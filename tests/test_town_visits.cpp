#include "test.hpp"
#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"

using namespace tp;

namespace {
struct VisitTowns {
    World w;
    CompanyId company;
    StationId a, b;
    VehicleId train;

    CommandResult run(CommandPayload payload) {
        Command c; c.company = company; c.payload = std::move(payload);
        return executeCommand(w, c);
    }

    VisitTowns() {
        for (i32 y = 0; y < 2; ++y)
            for (i32 x = 0; x < 2; ++x) w.map.activate({x, y});
        for (i32 y = 1; y < 64; ++y)
            for (i32 x = 1; x < 64; ++x) {
                w.map.setCornerHeight(x, y, 4);
                *w.map.tile({x, y}) = Tile{};
            }
        const auto created = run(CmdCreateCompany{"Visitors", 0xFF});
        company = CompanyId(static_cast<u8>(created.createdId));
        w.companies[company].money = 50'000'000;
        for (const i32 x : {10, 42}) {
            const auto town = w.towns.create();
            w.towns[town].centre = {x + 2, 26};
            w.towns[town].population = 200;
            w.towns[town].name = x == 10 ? "Origin" : "Destination";
            TrackAnchor anchor; anchor.pos = tileCentre({x, 20});
            const auto built = run(CmdBuildTrackStation{anchor, tileCentre({x + 8, 20})});
            REQUIRE(built.ok);
            const StationId station(built.createdId);
            if (x == 10) a = station; else b = station;
            REQUIRE(run(CmdBuildRoad{{x, 26}, {x + 8, 26}}).ok);
            REQUIRE(run(CmdStationEntrance{station, {x + 2, 26}}).ok);
            BuildingId home;
            for (const u16 type : {u16(0), u16(5), u16(8)}) {
                const auto id = w.buildings.create();
                auto& building = w.buildings[id];
                building.serial = ++w.nextSerial;
                building.type = type; building.town = town;
                building.pos = {x + 2 + (type == 0 ? 0 : type == 5 ? 2 : 4), 25};
                building.facing = Dir::S;
                auto& tile = *w.map.tile(building.pos);
                tile.type = TileType::Building; tile.index = id.value;
                w.towns[town].buildings.push_back(id);
                if (type == 0) home = id;
                if (type == 5) w.towns[town].retail.push_back(id);
                if (type == 8) w.towns[town].jobSites.push_back(id);
            }
            // Half have jobs; all own cars, to exercise the incentive to ride for visits.
            for (int i = 0; i < 200; ++i) {
                const auto id = people::create(w, home);
                auto& p = w.people[id];
                if (i < 100) p.work = w.buildings[w.towns[town].jobSites.front()].ref(w.towns[town].jobSites.front());
                p.carOwner = true;
                people::reschedule(w, id);
            }
        }
        passengers::updateRouting(w);
        w.clock.tick = 6 * kTicksPerHour;
    }

    void connect() {
        Vehicle v; v.model = 7; v.cars = {7, 11}; v.owner = company;
        v.freeTrack = true; v.stopped = false; v.state = VehicleState::Driving;
        Order first, second; first.station = a; second.station = b;
        v.orders = {first, second};
        train = w.vehicles.create(v);
        passengers::markDirty(w);
        passengers::updateRouting(w);
    }
};
}

TEST_CASE("town visits: new rail connections generate business and shopping passengers in both directions") {
    VisitTowns f;
    for (const auto [id, p] : f.w.people) CHECK(p.purpose != TripPurpose::Visit);
    f.connect();
    int business = 0, shopping = 0;
    for (const auto [id, p] : f.w.people) {
        if (p.purpose != TripPurpose::Visit) continue;
        REQUIRE(p.visit.valid());
        CHECK(f.w.buildings[p.visit.id].town != p.town);
        CHECK(p.nextEvent < 8 * kTicksPerDay);
        if (p.nextEvent >= kTicksPerDay) continue;
        business += p.employed(); shopping += !p.employed();
    }
    CHECK(business > 0); CHECK(shopping > 0);
    bool waitingA = false, waitingB = false;
    for (; f.w.clock.tick < 15 * kTicksPerHour; ++f.w.clock.tick) {
        people::process(f.w);
        waitingA |= f.w.stations[f.a].waitingCount() > 0;
        waitingB |= f.w.stations[f.b].waitingCount() > 0;
    }
    CHECK(waitingA); CHECK(waitingB);
}

TEST_CASE("town visits: inaccessible platforms do not generate outings and closing service refreshes plans") {
    VisitTowns f;
    const auto entrances = f.w.stations[f.b].entrances;
    f.w.stations[f.b].entrances.clear();
    f.connect();
    for (const auto [id, p] : f.w.people) CHECK(p.purpose != TripPurpose::Visit);
    f.w.stations[f.b].entrances = entrances;
    // Adding access after the train service exists must refresh demand even when routes match.
    passengers::markDirty(f.w); passengers::updateRouting(f.w);
    int visits = 0;
    for (const auto [id, p] : f.w.people) visits += p.purpose == TripPurpose::Visit;
    CHECK(visits > 0);
    f.w.vehicles[f.train].orders.clear();
    passengers::markDirty(f.w); passengers::updateRouting(f.w);
    for (const auto [id, p] : f.w.people) CHECK(p.purpose != TripPurpose::Visit);
}

TEST_CASE("town visits: first routing build refreshes plans but a missing return route prevents visits") {
    VisitTowns f;
    f.w.routing = RoutingTable{};
    f.connect();
    int visits = 0;
    for (const auto [id, p] : f.w.people) visits += p.purpose == TripPurpose::Visit;
    CHECK(visits > 0);
    f.w.routing.routes.erase(f.b.value);
    f.w.townLinks.routing = -2;
    for (const auto [id, p] : f.w.people) people::reschedule(f.w, id);
    for (const auto [id, p] : f.w.people) CHECK(p.purpose != TripPurpose::Visit);
}

TEST_CASE("town visits: planned visits survive save and load with identical departures") {
    VisitTowns f;
    f.connect();
    World restored;
    fromBytes(toBytes(f.w, kSaveVersion), restored, kSaveVersion);
    passengers::updateRouting(restored);
    CHECK_EQ(f.w.stateHash(), restored.stateHash());
    for (; f.w.clock.tick < 15 * kTicksPerHour; ++f.w.clock.tick) {
        restored.clock.tick = f.w.clock.tick;
        people::process(f.w); people::process(restored);
    }
    restored.clock.tick = f.w.clock.tick;
    CHECK_EQ(f.w.stateHash(), restored.stateHash());
}

TEST_CASE("town visits: a car owner boards, visits and takes transit home") {
    VisitTowns f;
    f.connect();
    PersonId visitor;
    for (const auto [id, p] : f.w.people)
        if (p.purpose == TripPurpose::Visit && p.nextEvent < kTicksPerDay &&
            p.town == f.w.stations[f.a].town &&
            (!visitor.valid() || p.nextEvent < f.w.people[visitor].nextEvent)) visitor = id;
    REQUIRE(visitor.valid());
    const auto advance = [&] {
        const auto tick = f.w.people[visitor].nextEvent;
        REQUIRE(tick >= f.w.clock.tick);
        f.w.clock.tick = tick;
        people::process(f.w);
    };
    advance();
    CHECK(f.w.people[visitor].state == PersonState::ToStop);
    advance();
    REQUIRE(f.w.people[visitor].state == PersonState::Waiting);
    auto& train = f.w.vehicles[f.train];
    train.cars.assign(12, 11); train.cars.front() = 7;
    train.freeTrack = false; train.orderIndex = 0; train.tile = f.w.stations[f.a].sign;
    passengers::exchangeAtStop(f.w, f.train, f.a);
    REQUIRE(f.w.people[visitor].state == PersonState::Riding);
    train.orderIndex = 1; train.tile = f.w.stations[f.b].sign;
    f.w.clock.tick += 30 * kTicksPerMinute;
    passengers::exchangeAtStop(f.w, f.train, f.b);
    REQUIRE(f.w.people[visitor].state == PersonState::FromStop);
    advance();
    CHECK(f.w.people[visitor].state == PersonState::AtShop || f.w.people[visitor].state == PersonState::AtWork);
    advance();
    CHECK(f.w.people[visitor].purpose == TripPurpose::Home);
    CHECK(f.w.people[visitor].state == PersonState::ToStop);
    advance();
    REQUIRE(f.w.people[visitor].state == PersonState::Waiting);
    passengers::exchangeAtStop(f.w, f.train, f.b);
    REQUIRE(f.w.people[visitor].state == PersonState::Riding);
    train.orderIndex = 0; train.tile = f.w.stations[f.a].sign;
    f.w.clock.tick += 30 * kTicksPerMinute;
    passengers::exchangeAtStop(f.w, f.train, f.a);
    advance();
    CHECK(f.w.people[visitor].state == PersonState::AtHome);
    CHECK(f.w.people[visitor].nextEvent >= f.w.clock.tickOfDay(f.w.clock.day() + 1));
}

TEST_CASE("town visits: all residents use town-wide rail access for daily work and shopping") {
    VisitTowns f;
    REQUIRE(f.run(CmdBuildRoad{{10, 26}, {29, 26}}).ok);
    REQUIRE(f.run(CmdBuildRoad{{42, 26}, {62, 26}}).ok);
    const auto originTown = f.w.stations[f.a].town;
    for (const auto [id, building] : f.w.buildings) {
        if ((building.town == originTown && building.info().category == BuildingCategory::Residential) ||
            (building.town != originTown && building.info().category != BuildingCategory::Residential)) {
            const TilePos destination{building.town == originTown ? 27 : building.type == 5 ? 58 : 60, 25};
            *f.w.map.tile(building.pos) = Tile{};
            f.w.buildings[id].pos = destination;
            auto& tile = *f.w.map.tile(destination);
            tile.type = TileType::Building; tile.index = id.value;
        }
    }
    f.connect();
    for (const auto [id, p] : f.w.people) {
        CHECK(p.purpose == TripPurpose::Visit);
        CHECK(p.nextEvent < kTicksPerDay);
    }
    for (; f.w.clock.tick < 20 * kTicksPerHour; ++f.w.clock.tick) people::process(f.w);
    CHECK_EQ(f.w.stations[f.a].waitingCount(), 200u);
    CHECK_EQ(f.w.stations[f.b].waitingCount(), 200u);
}

TEST_CASE("town visits: long routes and an afternoon opening still attract every resident today") {
    VisitTowns f;
    f.w.clock.tick = 14 * kTicksPerHour;
    f.connect();
    f.w.routing.routes[f.a.value][f.b.value].cost = 20 * kTicksPerHour;
    f.w.routing.routes[f.b.value][f.a.value].cost = 20 * kTicksPerHour;
    for (const auto [id, p] : f.w.people) people::reschedule(f.w, id);
    for (const auto [id, p] : f.w.people) {
        CHECK(p.purpose == TripPurpose::Visit);
        CHECK(p.nextEvent > f.w.clock.tick);
        CHECK(p.nextEvent <= f.w.clock.tick + 2 * kTicksPerHour);
    }
    for (; f.w.clock.tick < 20 * kTicksPerHour; ++f.w.clock.tick) people::process(f.w);
    CHECK_EQ(f.w.stations[f.a].waitingCount(), 200u);
    CHECK_EQ(f.w.stations[f.b].waitingCount(), 200u);
}

TEST_CASE("town visits: passengers wait days for late trains, survive loading and board when one arrives") {
    VisitTowns f;
    f.connect();
    for (; f.w.clock.tick < 2 * kTicksPerDay; ++f.w.clock.tick) people::process(f.w);
    REQUIRE(f.w.stations[f.a].waitingCount() == 200u);
    REQUIRE(f.w.stations[f.b].waitingCount() == 200u);
    World restored;
    fromBytes(toBytes(f.w, kSaveVersion), restored, kSaveVersion);
    passengers::updateRouting(restored);
    for (; f.w.clock.tick < 5 * kTicksPerDay; ++f.w.clock.tick) {
        restored.clock.tick = f.w.clock.tick;
        people::process(f.w); people::process(restored);
    }
    restored.clock.tick = f.w.clock.tick;
    CHECK_EQ(f.w.stateHash(), restored.stateHash());
    const PersonId passenger = f.w.stations[f.a].waiting.begin()->second.front();
    auto& train = f.w.vehicles[f.train];
    train.freeTrack = false; train.orderIndex = 0; train.tile = f.w.stations[f.a].sign;
    CHECK(passengers::exchangeAtStop(f.w, f.train, f.a) > 0);
    CHECK(f.w.people[passenger].state == PersonState::Riding);
    CHECK(f.w.stations[f.a].waitingCount() > 0);
    CHECK(f.w.stations[f.a].waitingCount() < 200u);
    // Passengers who could not fit remain queued; closing the service releases them.
    f.w.vehicles[f.train].orders.clear();
    passengers::markDirty(f.w); passengers::updateRouting(f.w);
    CHECK_EQ(f.w.stations[f.a].waitingCount(), 0u);
    CHECK_EQ(f.w.stations[f.b].waitingCount(), 0u);
}

TEST_CASE("town visits: previous saves refresh demand while current saves preserve departure times") {
    VisitTowns f;
    f.connect();
    for (const auto [id, p] : f.w.people) {
        f.w.people[id].purpose = TripPurpose::None;
        people::schedule(f.w, id, 7 * kTicksPerDay);
    }
    World previous;
    fromBytes(toBytes(f.w, 22), previous, 22);
    passengers::updateRouting(previous);
    for (const auto [id, p] : previous.people) {
        CHECK(p.purpose == TripPurpose::Visit);
        CHECK(p.nextEvent < kTicksPerDay);
    }
}
