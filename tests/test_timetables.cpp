#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>

using namespace tp;

namespace {

// A loop of company road on flat, empty land with two bus stops and a depot, in 1990 so every
// bus model is available.
struct Loop {
    World w;
    CompanyId co;
    StationId a, b;
    DepotId depot;

    static WorldSettings settings() {
        WorldSettings s;
        s.startYear = 1990;
        return s;
    }
    Loop() : w(settings()) {
        w.map.activate({0, 0});
        w.map.activate({1, 0});
        for (int y = 1; y < 32; ++y)
            for (int x = 1; x < 63; ++x) {
                *w.map.tile({x, y}) = Tile{};
                w.map.setCornerHeight(x, y, 4);
            }
        const CommandResult created = run(CmdCreateCompany{"Timetable Transit", 0});
        REQUIRE(created.ok);
        co = CompanyId(static_cast<u8>(created.createdId));
        w.companies[co].money = 50'000'000;
        REQUIRE(run(CmdBuildRoad{{5, 5}, {40, 5}, false}).ok);
        REQUIRE(run(CmdBuildRoad{{40, 5}, {40, 15}, false}).ok);
        REQUIRE(run(CmdBuildRoad{{40, 15}, {5, 15}, false}).ok);
        REQUIRE(run(CmdBuildRoad{{5, 15}, {5, 5}, false}).ok);
        REQUIRE(run(CmdBuildRoadStop{{15, 5}, false}).ok);
        REQUIRE(run(CmdBuildRoadStop{{30, 15}, false}).ok);
        a = StationId(w.map.tile({15, 5})->index);
        b = StationId(w.map.tile({30, 15})->index);
        const CommandResult d = run(CmdBuildRoadDepot{{10, 4}, Dir::S, false});
        REQUIRE(d.ok);
        depot = DepotId(d.createdId);
    }
    CommandResult run(CommandPayload payload) {
        Command c;
        c.company = co;
        c.payload = std::move(payload);
        return executeCommand(w, c);
    }
    std::vector<Order> orders(u8 flagsAtA = 0, u16 interval = 0, u16 wait = 0) const {
        Order oa, ob;
        oa.station = a;
        oa.flags = flagsAtA;
        oa.intervalMinutes = interval;
        oa.waitMinutes = wait;
        ob.station = b;
        return {oa, ob};
    }
    // Buys `count` buses sharing one order list and starts them together.
    std::vector<VehicleId> buses(int count, const std::vector<Order>& list, u16 model = 0) {
        const CommandResult bought = run(CmdBuyVehicle{depot, model});
        REQUIRE(bought.ok);
        std::vector<VehicleId> out{VehicleId(bought.createdId)};
        REQUIRE(run(CmdSetOrders{out.front(), list}).ok);
        for (int i = 1; i < count; ++i) {
            const CommandResult clone = run(CmdCloneVehicle{depot, out.front(), true});
            REQUIRE(clone.ok);
            out.push_back(VehicleId(clone.createdId));
        }
        for (const VehicleId id : out) REQUIRE(run(CmdStartStopVehicle{id}).ok);
        return out;
    }
    // Runs the world, noting each time a bus leaves stop A.
    std::vector<i64> departuresFromA(const std::vector<VehicleId>& fleet, i64 ticks) {
        std::vector<i64> out;
        std::map<u32, bool> loadingAtA;
        for (i64 t = 0; t < ticks; ++t) {
            w.tick();
            for (const VehicleId id : fleet) {
                const Vehicle& v = w.vehicles[id];
                const bool atA = v.state == VehicleState::Loading && v.currentOrder() && v.currentOrder()->station == a;
                if (loadingAtA[id.value] && !atA) out.push_back(w.clock.tick);
                loadingAtA[id.value] = atA;
            }
        }
        return out;
    }
};

i64 smallestGap(const std::vector<i64>& ticks, usize from) {
    i64 gap = std::numeric_limits<i64>::max();
    for (usize i = std::max<usize>(from, 1); i < ticks.size(); ++i) gap = std::min(gap, ticks[i] - ticks[i - 1]);
    return gap;
}

} // namespace

TEST_CASE("timetables: a timing point with an interval spaces departures at least that far apart") {
    Loop f;
    const auto fleet = f.buses(2, f.orders(kOrderTimingPoint, 60));
    const auto departures = f.departuresFromA(fleet, 6000);
    REQUIRE(departures.size() >= 6);
    CHECK(smallestGap(departures, 1) >= 60 * kTicksPerMinute);
    // Both buses keep running.
    for (const VehicleId id : fleet) CHECK(f.w.vehicles[id].departures.size() == 2 && f.w.vehicles[id].departures[0] > 0);
}

TEST_CASE("timetables: automatic spacing shares out the measured round trip between the buses") {
    Loop bunched;
    const auto plain = bunched.buses(3, bunched.orders());
    const auto together = bunched.departuresFromA(plain, 12000);
    REQUIRE(together.size() >= 9);
    // Started together, the buses stay bunched.
    CHECK(smallestGap(together, together.size() - 6) < 10 * kTicksPerMinute);

    Loop f;
    const auto fleet = f.buses(3, f.orders(kOrderTimingPoint));
    const auto departures = f.departuresFromA(fleet, 12000);
    REQUIRE(departures.size() >= 9);
    const vehicles::ServiceSpacing sp = vehicles::spacing(f.w, fleet.front(), 0);
    CHECK_EQ(sp.vehicles, 3);
    REQUIRE(sp.roundTrip > 0);
    CHECK(sp.interval > 0);
    CHECK(std::abs(sp.interval - sp.roundTrip / 3) <= 1);
    // Once measured, the last departures are close to a third of the round trip apart.
    for (usize i = departures.size() - 4; i < departures.size(); ++i) {
        const i64 gap = departures[i] - departures[i - 1];
        CHECK(gap >= sp.interval * 8 / 10);
        CHECK(gap <= sp.interval * 13 / 10);
    }
}

TEST_CASE("timetables: a timetabled stop keeps the bus at least that long") {
    Loop f;
    const auto fleet = f.buses(1, f.orders(0, 0, 45));
    const Vehicle& v = f.w.vehicles[fleet.front()];
    i64 arrived = -1, left = -1;
    for (int t = 0; t < 4000 && left < 0; ++t) {
        const bool before = v.state == VehicleState::Loading && v.currentOrder()->station == f.a;
        f.w.tick();
        const bool after = v.state == VehicleState::Loading && v.currentOrder()->station == f.a;
        if (!before && after) arrived = f.w.clock.tick;
        if (before && !after && arrived >= 0) left = f.w.clock.tick;
    }
    REQUIRE(arrived >= 0);
    REQUIRE(left >= 0);
    CHECK(left - arrived >= 45 * kTicksPerMinute);
    CHECK(left - arrived <= 45 * kTicksPerMinute + 2);
}

TEST_CASE("timetables: only stops are timetabled, within a day, and new orders start afresh") {
    Loop f;
    const auto fleet = f.buses(1, f.orders(kOrderTimingPoint));
    std::vector<Order> list = f.orders();
    list.push_back({});
    list.back().kind = OrderKind::Depot;
    list.back().depot = f.depot;
    list.back().flags = kOrderTimingPoint;
    CHECK(!f.run(CmdSetOrders{fleet.front(), list}).ok);
    list.back().flags = 0;
    list.back().waitMinutes = 5;
    CHECK(!f.run(CmdSetOrders{fleet.front(), list}).ok);
    list.back().waitMinutes = 0;
    list[1].flags = kOrderNonStop;
    list[1].waitMinutes = 5;
    CHECK(!f.run(CmdSetOrders{fleet.front(), list}).ok);
    CHECK(!f.run(CmdSetOrders{fleet.front(), f.orders(kOrderTimingPoint, kMaxTimetableMinutes + 1)}).ok);
    CHECK(f.run(CmdSetOrders{fleet.front(), f.orders(kOrderTimingPoint, kMaxTimetableMinutes)}).ok);

    f.departuresFromA(fleet, 3000);
    REQUIRE(!f.w.vehicles[fleet.front()].departures.empty());
    REQUIRE(f.run(CmdSetOrders{fleet.front(), f.orders(kOrderTimingPoint)}).ok);
    CHECK(f.w.vehicles[fleet.front()].departures.empty());
    CHECK_EQ(f.w.vehicles[fleet.front()].cycleTicks, 0);
}

TEST_CASE("timetables: timetables survive saving and older saves load without them") {
    Loop f;
    const auto fleet = f.buses(2, f.orders(kOrderTimingPoint, 0, 10));
    f.departuresFromA(fleet, 2500);
    REQUIRE(f.w.vehicles[fleet.front()].cycleTicks > 0 || f.w.vehicles[fleet.back()].cycleTicks > 0);
    World copy;
    fromBytes(toBytes(f.w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(copy.stateHash(), f.w.stateHash());
    CHECK(copy.vehicles[fleet.front()].orders == f.w.vehicles[fleet.front()].orders);
    for (int i = 0; i < 1500; ++i) {
        f.w.tick();
        copy.tick();
    }
    CHECK_EQ(copy.stateHash(), f.w.stateHash());
    World legacy;
    fromBytes(toBytes(f.w, 28), legacy, 28);
    CHECK_EQ(legacy.vehicles[fleet.front()].orders.front().waitMinutes, 0);
    CHECK(legacy.vehicles[fleet.front()].departures.empty());
    CHECK(legacy.companies[f.co].replacements.empty());
}

TEST_CASE("autoreplace: a bus is replaced by the chosen model at its depot visit, keeping its orders") {
    Loop f;
    const auto fleet = f.buses(1, f.orders());
    const VehicleId bus = fleet.front();
    const u16 unit = f.w.vehicles[bus].unitNumber;
    f.departuresFromA(fleet, 600);
    CHECK(!vehicles::plannedReplacement(f.w, f.w.vehicles[bus]));
    REQUIRE(f.run(CmdSetAutoreplace{0, 2}).ok);
    const auto planned = vehicles::plannedReplacement(f.w, f.w.vehicles[bus]);
    REQUIRE(planned);
    CHECK_EQ(planned->model, 2);
    const Money before = f.w.companies[f.co].money;
    const Money traded = f.w.vehicles[bus].value;
    bool replaced = false;
    for (int t = 0; t < 3 * kTicksPerDay && !replaced; ++t) {
        f.w.tick();
        replaced = f.w.vehicles[bus].model == 2;
    }
    REQUIRE(replaced);
    const Vehicle& v = f.w.vehicles[bus];
    CHECK_EQ(v.unitNumber, unit);
    CHECK(v.orders == f.orders());
    CHECK_EQ(v.value, f.w.inflated(vehicleModel(2).price));
    CHECK_EQ(v.builtTick, v.lastServiceTick);
    // Paid the new price less the trade-in (and a little running cost).
    const Money paid = before - f.w.companies[f.co].money;
    CHECK(paid >= v.value - traded);
    CHECK(paid < v.value - traded + 2'000);
    CHECK(!vehicles::plannedReplacement(f.w, v));
    // It carries on with its orders.
    for (int t = 0; t < 600; ++t) f.w.tick();
    CHECK(v.state != VehicleState::InDepot);
}

TEST_CASE("autoreplace: autorenew renews old vehicles and waits for money") {
    Loop f;
    const auto fleet = f.buses(1, f.orders());
    const VehicleId bus = fleet.front();
    f.departuresFromA(fleet, 300);
    Vehicle& v = f.w.vehicles[bus];
    v.builtTick = f.w.clock.tick - (v.info().lifeYears * 365 + 10) * kTicksPerDay;
    v.value = 0; // worn out
    CHECK(!vehicles::plannedReplacement(f.w, v));
    REQUIRE(f.run(CmdSetAutorenew{true}).ok);
    REQUIRE(vehicles::plannedReplacement(f.w, v));

    f.w.companies[f.co].money = 0;
    const usize news = f.w.news.size();
    vehicles::autoreplace(f.w, bus);
    vehicles::autoreplace(f.w, bus);
    CHECK(vehicles::ageDays(f.w, v) > v.info().lifeYears * 365);
    CHECK_EQ(f.w.news.size(), news + 1); // one warning, not one per visit

    f.w.companies[f.co].money = 10'000'000;
    bool renewed = false;
    for (int t = 0; t < 3 * kTicksPerDay && !renewed; ++t) {
        f.w.tick();
        renewed = vehicles::ageDays(f.w, v) < 2;
    }
    CHECK(renewed);
    CHECK_EQ(v.model, 0);
    CHECK_EQ(v.reliability, maxReliability(v.info()));
}

TEST_CASE("autoreplace: rules need interchangeable, available models") {
    Loop f;
    const auto find = [](std::string_view name) {
        for (u16 m = 0; m < vehicleModels().size(); ++m)
            if (vehicleModel(m).name == name) return m;
        return kNoReplacement;
    };
    const u16 bus = find("Albatross Bus"), lorry = find("Grain Lorry"), tram = find("Tramcar Mk1");
    const u16 wooden = find("Wooden Coach"), steel = find("Steel Coach"), hopper = find("Grain Hopper");
    const u16 kirby = find("Kirby Tank Engine"), diesel = find("Ploddyphut Diesel");
    CHECK(!f.run(CmdSetAutoreplace{bus, lorry}).ok);
    CHECK(!f.run(CmdSetAutoreplace{bus, tram}).ok);
    CHECK(!f.run(CmdSetAutoreplace{bus, bus}).ok);
    CHECK(!f.run(CmdSetAutoreplace{wooden, hopper}).ok);
    CHECK(!f.run(CmdSetAutoreplace{kirby, steel}).ok);
    CHECK(f.run(CmdSetAutoreplace{wooden, steel}).ok);
    CHECK(f.run(CmdSetAutoreplace{kirby, diesel}).ok);
    CHECK_EQ(f.w.companies[f.co].replacements.size(), 2u);
    CHECK(f.run(CmdSetAutoreplace{kirby, kNoReplacement}).ok);
    CHECK_EQ(f.w.companies[f.co].replacements.size(), 1u);

    // A train swaps only the carriages with a rule.
    Vehicle train;
    train.owner = f.co;
    train.model = kirby;
    train.cars = {kirby, wooden, wooden};
    train.builtTick = f.w.clock.tick;
    const auto planned = vehicles::plannedReplacement(f.w, train);
    REQUIRE(planned);
    CHECK((planned->cars == std::vector<u16>{kirby, steel, steel}));
    CHECK_EQ(planned->price, f.w.inflated(vehicleModel(kirby).price) + 2 * f.w.inflated(vehicleModel(steel).price));

    // Not yet invented.
    WorldSettings early;
    early.startYear = 1950;
    World old(early);
    Command c;
    c.payload = CmdCreateCompany{"Early", 0};
    const CommandResult created = executeCommand(old, c);
    REQUIRE(created.ok);
    c.company = CompanyId(static_cast<u8>(created.createdId));
    c.payload = CmdSetAutoreplace{bus, find("Leopard Bus")};
    CHECK(!executeCommand(old, c).ok);
}
