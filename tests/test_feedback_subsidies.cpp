#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/passengers.hpp"
#include "sim/world.hpp"

#include <algorithm>

using namespace tp;

namespace {

// Two towns 40 tiles apart, each with a station of one company, and a bus.
struct Towns {
    World w;
    CompanyId co, rival;
    TownId a, b;
    StationId sa, sb;
    VehicleId bus;

    Towns() {
        w.map.activate({0, 0});
        w.map.activate({1, 0});
        co = company("Happy Buses");
        rival = company("Rival Lines");
        a = town("Ashby", {10, 10});
        b = town("Brookfield", {50, 10});
        sa = station(a, {10, 12});
        sb = station(b, {50, 12});
        Vehicle v;
        v.owner = co;
        v.model = 0;
        v.unitNumber = 4;
        bus = w.vehicles.create(v);
    }
    CompanyId company(const char* name) {
        Command c;
        c.payload = CmdCreateCompany{name, 0xFF};
        const CommandResult r = executeCommand(w, c);
        REQUIRE(r.ok);
        return CompanyId(static_cast<u8>(r.createdId));
    }
    TownId town(const char* name, TilePos centre) {
        const TownId id = w.towns.create();
        w.towns[id].name = name;
        w.towns[id].centre = centre;
        w.towns[id].population = 400;
        return id;
    }
    StationId station(TownId town, TilePos sign) {
        const StationId id = w.stations.create();
        Station& s = w.stations[id];
        s.owner = co;
        s.town = town;
        s.sign = sign;
        s.tiles = {sign};
        s.facilities = kFacilityBus;
        s.name = w.towns[town].name + " Halt";
        return id;
    }
    PersonId person(u32 serial) {
        Person p;
        p.serial = serial;
        p.nameSeed = serial * 7919u;
        p.town = a;
        return w.people.create(p);
    }
    usize comments(FeedbackKind kind) const {
        return static_cast<usize>(std::ranges::count_if(w.feedback, [&](const Feedback& f) { return f.kind == kind; }));
    }
};

} // namespace

TEST_CASE("feedback: waits, crowding and slow rides lower the score") {
    CHECK_EQ(feedback::waitScore(0), 100);
    CHECK_EQ(feedback::waitScore(5 * kTicksPerMinute), 100);
    CHECK(feedback::waitScore(30 * kTicksPerMinute) < 60);
    CHECK_EQ(feedback::waitScore(60 * kTicksPerMinute), 0);
    CHECK_EQ(feedback::rideScore(10, 100), 100);
    CHECK(feedback::rideScore(10, 280) < 100);
    CHECK_EQ(feedback::rideScore(10, 420), 0);

    Towns t;
    const PersonId quick = t.person(1), slow = t.person(2);
    feedback::boarded(t.w, quick, t.bus, t.sa, 2 * kTicksPerMinute);
    CHECK_EQ(t.w.people[quick].boardScore, 100);
    feedback::alighted(t.w, quick, t.bus, t.sa, t.sb, 40, 300);
    feedback::boarded(t.w, slow, t.bus, t.sa, 50 * kTicksPerMinute);
    CHECK(t.w.people[slow].boardScore < 20);
    feedback::alighted(t.w, slow, t.bus, t.sa, t.sb, 40, 2000);
    const Vehicle& v = t.w.vehicles[t.bus];
    CHECK_EQ(v.satisfactionCount[0], 2);
    CHECK(feedback::satisfaction(v) > 40);
    CHECK(feedback::satisfaction(v) < 60);
    // A crowded bus costs points even after a short wait.
    for (u32 i = 0; i < v.capacity(); ++i) t.w.vehicles[t.bus].passengers.push_back(PersonId(i + 100));
    const PersonId squeezed = t.person(3);
    feedback::boarded(t.w, squeezed, t.bus, t.sa, 0);
    CHECK_EQ(t.w.people[squeezed].boardScore, 75);
    // Months roll over: last month still counts, the month before does not.
    feedback::monthly(t.w);
    CHECK_EQ(t.w.vehicles[t.bus].satisfactionCount[0], 0);
    CHECK_EQ(t.w.vehicles[t.bus].satisfactionCount[1], 2);
    CHECK(feedback::satisfaction(t.w.vehicles[t.bus]) > 0);
    feedback::monthly(t.w);
    CHECK_EQ(feedback::satisfaction(t.w.vehicles[t.bus]), -1);
}

TEST_CASE("feedback: some passengers speak up, a comment of each kind at most every two hours") {
    Towns t;
    for (u32 i = 0; i < 40; ++i) feedback::boarded(t.w, t.person(10 + i), t.bus, t.sa, 50 * kTicksPerMinute);
    CHECK_EQ(t.comments(FeedbackKind::LongWait), 1u);
    const Feedback& f = t.w.feedback.back();
    CHECK(f.company == t.co);
    CHECK(f.station == t.sa);
    CHECK_EQ(f.unit, 4);
    CHECK_EQ(f.minutes, 50);
    CHECK(!isPraise(f.kind));
    for (u32 i = 0; i < 40; ++i) feedback::boarded(t.w, t.person(60 + i), t.bus, t.sa, 0);
    CHECK_EQ(t.comments(FeedbackKind::ShortWait), 1u);
    CHECK(isPraise(t.w.feedback.back().kind));
    t.w.clock.tick += 2 * kTicksPerHour;
    for (u32 i = 0; i < 40; ++i) feedback::boarded(t.w, t.person(110 + i), t.bus, t.sa, 50 * kTicksPerMinute);
    CHECK_EQ(t.comments(FeedbackKind::LongWait), 2u);
    // Waiting too long and giving up is a complaint about the station's owner.
    for (u32 i = 0; i < 40; ++i) {
        const PersonId p = t.person(200 + i);
        t.w.people[p].state = PersonState::Waiting;
        t.w.people[p].station = t.sa;
        t.w.people[p].targetStation = t.sb;
        t.w.people[p].boardTick = t.w.clock.tick - 3 * kTicksPerHour;
        t.w.stations[t.sa].waiting[t.sb].push_back(p);
        passengers::giveUpWaiting(t.w, p);
    }
    CHECK_EQ(t.comments(FeedbackKind::GaveUp), 1u);
    CHECK_EQ(t.w.feedback.back().minutes, 180);
    // The feed keeps the newest comments.
    for (u32 i = 0; i < 1000; ++i) {
        t.w.clock.tick += 2 * kTicksPerHour;
        for (u32 k = 0; k < 3; ++k) feedback::boarded(t.w, t.person(1000 + i * 3 + k), t.bus, t.sa, 50 * kTicksPerMinute);
    }
    CHECK_EQ(t.w.feedback.size(), kMaxFeedback);
    CHECK(t.w.feedback.front().tick > 4 * kTicksPerHour);
    CHECK(t.w.feedback.front().tick < t.w.feedback.back().tick);
}

TEST_CASE("subsidies: offers appear for unlinked towns and lapse when nobody takes them") {
    Towns t;
    for (int day = 0; day < 400 && t.w.subsidies.empty(); ++day) {
        t.w.clock.tick += kTicksPerDay;
        subsidies::daily(t.w);
    }
    REQUIRE(t.w.subsidies.size() == 1);
    const Subsidy offer = t.w.subsidies.front();
    CHECK(offer.cargo == CargoType::None);
    CHECK(!offer.awarded());
    CHECK((offer.fromTown == t.a && offer.toTown == t.b) || (offer.fromTown == t.b && offer.toTown == t.a));
    CHECK(t.w.news.back().text.find("Subsidy offered") != std::string::npos);
    CHECK_EQ(offer.untilTick - offer.offeredTick, kSubsidyOfferDays * kTicksPerDay);
    // Never the same route twice; nothing more once every pair is on offer.
    for (int day = 0; day < 100; ++day) {
        t.w.clock.tick += kTicksPerDay;
        subsidies::daily(t.w);
    }
    CHECK(t.w.subsidies.size() <= 2);
    t.w.clock.tick = offer.untilTick + kTicksPerDay * 100;
    subsidies::daily(t.w);
    CHECK(std::ranges::none_of(t.w.subsidies, [&](const Subsidy& s) { return s.offeredTick == offer.offeredTick; }));
}

TEST_CASE("subsidies: the first company to carry them wins double pay for a year") {
    Towns t;
    Subsidy s;
    s.fromTown = t.a;
    s.toTown = t.b;
    s.untilTick = t.w.clock.tick + kSubsidyOfferDays * kTicksPerDay;
    t.w.subsidies.push_back(s);
    CHECK_EQ(subsidies::passengerPay(t.w, t.co, t.sb, t.sa, 100), 100); // the other way round
    CHECK(!t.w.subsidies.front().awarded());
    CHECK_EQ(subsidies::passengerPay(t.w, t.co, t.sa, t.sb, 100), 200);
    REQUIRE(t.w.subsidies.front().awarded());
    CHECK(t.w.subsidies.front().awardedTo == t.co);
    CHECK_EQ(t.w.subsidies.front().untilTick, t.w.clock.tick + kSubsidyAwardDays * kTicksPerDay);
    CHECK(t.w.news.back().text.find("Happy Buses") != std::string::npos);
    CHECK_EQ(subsidies::passengerPay(t.w, t.rival, t.sa, t.sb, 100), 100);
    CHECK_EQ(subsidies::passengerPay(t.w, t.co, t.sa, t.sb, 70), 140);

    // Cargo: a producer's batch delivered to the subsidised town.
    const BuildingId dairy = t.w.buildings.create(), shop = t.w.buildings.create();
    t.w.buildings[dairy].serial = 501;
    t.w.buildings[shop].serial = 502;
    t.w.buildings[shop].town = t.b;
    Subsidy cheese;
    cheese.cargo = CargoType::Cheese;
    cheese.source = t.w.buildings[dairy].ref(dairy);
    cheese.toTown = t.b;
    cheese.untilTick = t.w.clock.tick + kTicksPerDay;
    t.w.subsidies.push_back(cheese);
    CargoBatch batch;
    batch.type = CargoType::Cheese;
    batch.producer = cheese.source;
    batch.receiver = t.w.buildings[shop].ref(shop);
    CHECK_EQ(subsidies::cargoPay(t.w, t.rival, batch, t.w.buildings[shop], 50), 100);
    CHECK(t.w.subsidies.back().awardedTo == t.rival);
    batch.type = CargoType::Milk;
    CHECK_EQ(subsidies::cargoPay(t.w, t.rival, batch, t.w.buildings[shop], 50), 50);
    CHECK(subsidies::describe(t.w, t.w.subsidies.back()).starts_with("cheese from"));

    // Saved with the world.
    World copy;
    fromBytes(toBytes(t.w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(copy.stateHash(), t.w.stateHash());
    CHECK_EQ(copy.subsidies.size(), 2u);

    // The double pay ends after a year.
    t.w.clock.tick += kSubsidyAwardDays * kTicksPerDay + 1;
    subsidies::daily(t.w);
    CHECK(std::ranges::none_of(t.w.subsidies, [](const Subsidy& x) { return x.awarded(); }));
    CHECK(t.w.news.back().text.find("run out") != std::string::npos ||
          t.w.news.back().text.find("Subsidy offered") != std::string::npos);
}

TEST_CASE("subsidies: an explored world offers passenger and cargo subsidies") {
    WorldSettings settings;
    settings.seed = 20261017;
    World w(settings);
    for (i32 y = -6; y <= 6; ++y)
        for (i32 x = -6; x <= 6; ++x) w.activateChunk({x, y});
    cargo::daily(w); // registers the industries
    for (Industry& i : w.industries) i.producedLastMonth = 100;
    bool passengers = false, freight = false;
    for (int day = 0; day < 365 && !(passengers && freight); ++day) {
        w.clock.tick += kTicksPerDay;
        subsidies::daily(w);
        for (const Subsidy& s : w.subsidies) (s.cargo == CargoType::None ? passengers : freight) = true;
        if (w.subsidies.size() >= kMaxSubsidyOffers) w.subsidies.clear(); // make room for more
    }
    CHECK(passengers);
    CHECK(freight);
}
