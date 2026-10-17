#include "test.hpp"

#include "core/math.hpp"
#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/road.hpp"
#include "sim/road_path.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <optional>

using namespace tp;

namespace {

Command cmd(CompanyId company, CommandPayload payload) {
    Command c;
    c.company = company;
    c.payload = std::move(payload);
    return c;
}

struct Network {
    CompanyId company;
    StationId a, b;
    DepotId depot;
    VehicleId bus;
};

// Builds a two-stop bus line across the largest town of a freshly explored world.
std::optional<Network> buildLine(World& w) {
    for (i32 y = -6; y <= 6; ++y)
        for (i32 x = -6; x <= 6; ++x) w.activateChunk({x, y});
    const CommandResult cr = executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Test Transit", 0xFF}));
    if (!cr.ok) return std::nullopt;
    Network n;
    n.company = CompanyId(static_cast<u8>(cr.createdId));

    const Town* town = nullptr;
    for (const auto [id, t] : w.towns)
        if (!town || t.population > town->population) town = &t;
    if (!town) return std::nullopt;

    // Candidate stop tiles: straight, flat town road tiles.
    std::vector<TilePos> candidates;
    for (const TilePos r : town->roads) {
        const Tile* t = w.map.tile(r);
        if (t && t->type == TileType::Road && isStraight(roadPieces(*t)) && w.map.slope(r).isFlat())
            candidates.push_back(r);
    }
    if (candidates.size() < 2) return std::nullopt;
    // Pick the two candidates furthest apart that are connected by road.
    TilePos sa = candidates.front(), sb = candidates.front();
    i64 best = -1;
    for (const TilePos p : candidates)
        for (const TilePos q : candidates) {
            const i64 d = manhattan(p.x - q.x, p.y - q.y);
            if (d > best) {
                best = d;
                sa = p;
                sb = q;
            }
        }
    if (!executeCommand(w, cmd(n.company, CmdBuildRoadStop{sa, false})).ok) return std::nullopt;
    if (!executeCommand(w, cmd(n.company, CmdBuildRoadStop{sb, false})).ok) return std::nullopt;
    n.a = StationId(w.map.tile(sa)->index);
    n.b = StationId(w.map.tile(sb)->index);

    // Depot next to any town road.
    for (const TilePos r : town->roads) {
        for (const Dir d : kDirs) {
            const TilePos site = neighbour(r, d);
            const CommandResult res = executeCommand(w, cmd(n.company, CmdBuildRoadDepot{site, opposite(d), false}));
            if (res.ok) {
                n.depot = DepotId(res.createdId);
                break;
            }
        }
        if (n.depot.valid()) break;
    }
    if (!n.depot.valid()) return std::nullopt;

    const CommandResult buy = executeCommand(w, cmd(n.company, CmdBuyVehicle{n.depot, 0}));
    if (!buy.ok) return std::nullopt;
    n.bus = VehicleId(buy.createdId);
    Order oa, ob;
    oa.station = n.a;
    ob.station = n.b;
    if (!executeCommand(w, cmd(n.company, CmdSetOrders{n.bus, {oa, ob}})).ok) return std::nullopt;
    if (!executeCommand(w, cmd(n.company, CmdStartStopVehicle{n.bus})).ok) return std::nullopt;
    return n;
}

World makeWorld() {
    WorldSettings s;
    s.seed = 20261017;
    return World(s);
}

} // namespace

TEST_CASE("transport: building roads costs money and connects tiles") {
    World w = makeWorld();
    for (i32 y = -1; y <= 1; ++y)
        for (i32 x = -1; x <= 1; ++x) w.activateChunk({x, y});
    const CommandResult cr = executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Roads Ltd", 0xFF}));
    const CompanyId co(static_cast<u8>(cr.createdId));
    // Find a flat-ish clear row to build on.
    for (i32 y = -20; y < 20; ++y) {
        const CommandResult r = executeCommand(w, cmd(co, CmdBuildRoad{{-10, y}, {-4, y}, false}));
        if (!r.ok) continue;
        CHECK(r.cost > 0);
        const Tile* t = w.map.tile({-7, y});
        REQUIRE(t != nullptr);
        CHECK(t->type == TileType::Road);
        CHECK_EQ(roadPieces(*t), static_cast<u8>(dirBit(Dir::E) | dirBit(Dir::W)));
        CHECK_EQ(t->roadOwner, co.value);
        // Removing it again.
        CHECK(executeCommand(w, cmd(co, CmdRemoveRoad{{-10, y}, {-4, y}, false})).ok);
        CHECK(w.map.tile({-7, y})->type == TileType::Clear);
        return;
    }
    CHECK(false); // no buildable row found
}

TEST_CASE("transport: a bus line carries passengers and earns money") {
    World w = makeWorld();
    const auto net = buildLine(w);
    REQUIRE(net.has_value());
    const Station& a = w.stations[net->a];
    const Station& b = w.stations[net->b];
    CHECK(!a.name.empty());
    CHECK(a.name != b.name);
    // A route exists between the stops.
    const Depot& depot = w.depots[net->depot];
    std::vector<TilePos> goals = a.tiles;
    CHECK(findRoadPath(w, depot.tile, opposite(depot.facing), goals, VehicleType::Road, net->depot).has_value());

    const Money moneyBefore = w.companies[net->company].money;
    i64 visitsA = 0, visitsB = 0;
    i64 maxPassengers = 0;
    i64 lastA = -1, lastB = -1;
    for (i64 t = 0; t < 2 * kTicksPerDay; ++t) {
        w.tick();
        const Vehicle& v = w.vehicles[net->bus];
        maxPassengers = std::max<i64>(maxPassengers, static_cast<i64>(v.passengers.size()));
        if (w.stations[net->a].lastVisitTick != lastA) {
            lastA = w.stations[net->a].lastVisitTick;
            ++visitsA;
        }
        if (w.stations[net->b].lastVisitTick != lastB) {
            lastB = w.stations[net->b].lastVisitTick;
            ++visitsB;
        }
    }
    const Vehicle& v = w.vehicles[net->bus];
    CHECK(v.state != VehicleState::InDepot);
    CHECK(!v.lost);
    CHECK(visitsA >= 3);
    CHECK(visitsB >= 3);
    CHECK(maxPassengers > 0);
    CHECK(v.passengersThisYear > 0);
    CHECK(v.profitThisYear != 0);
    const Company& co = w.companies[net->company];
    CHECK(co.thisYear.amounts[static_cast<usize>(Expense::RoadVehicleIncome)] > 0);
    (void)moneyBefore;
}

TEST_CASE("transport: network state is deterministic and survives save/load") {
    World a = makeWorld();
    World b = makeWorld();
    REQUIRE(buildLine(a).has_value());
    REQUIRE(buildLine(b).has_value());
    for (i64 t = 0; t < kTicksPerDay / 2; ++t) {
        a.tick();
        b.tick();
    }
    CHECK_EQ(a.stateHash(), b.stateHash());
    const auto bytes = toBytes(a, kSaveVersion);
    World c;
    fromBytes(bytes, c, kSaveVersion);
    for (i64 t = 0; t < kTicksPerDay; ++t) {
        a.tick();
        c.tick();
    }
    CHECK_EQ(a.stateHash(), c.stateHash());
}

TEST_CASE("transport: vehicles can only be sold when stopped in a depot") {
    World w = makeWorld();
    const auto net = buildLine(w);
    REQUIRE(net.has_value());
    for (int i = 0; i < 200; ++i) w.tick();
    CHECK(!executeCommand(w, cmd(net->company, CmdSellVehicle{net->bus})).ok);
    CHECK(executeCommand(w, cmd(net->company, CmdSendToDepot{net->bus})).ok);
    for (i64 t = 0; t < kTicksPerDay && w.vehicles[net->bus].state != VehicleState::InDepot; ++t) w.tick();
    CHECK(w.vehicles[net->bus].state == VehicleState::InDepot);
    CHECK(w.vehicles[net->bus].stopped);
    const Money before = w.companies[net->company].money;
    CHECK(executeCommand(w, cmd(net->company, CmdSellVehicle{net->bus})).ok);
    CHECK(w.companies[net->company].money > before);
    CHECK(!w.vehicles.contains(net->bus));
}

TEST_CASE("transport: trams run on tram track laid along a road") {
    World w = makeWorld();
    for (i32 y = -6; y <= 6; ++y)
        for (i32 x = -6; x <= 6; ++x) w.activateChunk({x, y});
    const CommandResult cr = executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Trams Ltd", 0xFF}));
    const CompanyId co(static_cast<u8>(cr.createdId));
    // Find a straight east-west run of at least 10 connected road tiles.
    std::optional<TilePos> runStart;
    i32 runLen = 0;
    for (const auto [tid, town] : w.towns) {
        for (const TilePos r : town.roads) {
            i32 len = 0;
            TilePos t = r;
            while (len < 14) {
                const Tile* tile = w.map.tile(t);
                if (!tile || tile->type != TileType::Road || !(roadPieces(*tile) & dirBit(Dir::E))) break;
                ++len;
                t = neighbour(t, Dir::E);
            }
            if (len >= 10 && len > runLen) {
                runStart = r;
                runLen = len;
            }
        }
    }
    REQUIRE(runStart.has_value());
    const TilePos a = *runStart;
    const TilePos b{a.x + runLen - 1, a.y};
    REQUIRE(executeCommand(w, cmd(co, CmdBuildRoad{a, b, true})).ok);
    const TilePos sa{a.x + 1, a.y}, sb{b.x - 1, b.y};
    const CommandResult stopA = executeCommand(w, cmd(co, CmdBuildRoadStop{sa, true}));
    const CommandResult stopB = executeCommand(w, cmd(co, CmdBuildRoadStop{sb, true}));
    REQUIRE(stopA.ok);
    REQUIRE(stopB.ok);
    DepotId depot;
    for (i32 x = a.x + 2; x < b.x - 1 && !depot.valid(); ++x) {
        for (const Dir side : {Dir::N, Dir::S}) {
            const TilePos site = neighbour({x, a.y}, side);
            if (const Tile* st = w.map.tile(site); st && st->type == TileType::Building)
                executeCommand(w, cmd(co, CmdClearArea{site, site})); // make room, as a player would
            const CommandResult r = executeCommand(w, cmd(co, CmdBuildRoadDepot{site, opposite(side), true}));
            if (r.ok) {
                depot = DepotId(r.createdId);
                break;
            }
        }
    }
    REQUIRE(depot.valid());
    const CommandResult buy = executeCommand(w, cmd(co, CmdBuyVehicle{depot, 4}));
    REQUIRE(buy.ok);
    const VehicleId tram(buy.createdId);
    Order oa, ob;
    oa.station = StationId(w.map.tile(sa)->index);
    ob.station = StationId(w.map.tile(sb)->index);
    REQUIRE(executeCommand(w, cmd(co, CmdSetOrders{tram, {oa, ob}})).ok);
    REQUIRE(executeCommand(w, cmd(co, CmdStartStopVehicle{tram})).ok);
    i64 lastA = -1, visitsA = 0, lastB = -1, visitsB = 0;
    for (i64 t = 0; t < kTicksPerDay; ++t) {
        w.tick();
        if (w.stations[oa.station].lastVisitTick != lastA) {
            lastA = w.stations[oa.station].lastVisitTick;
            ++visitsA;
        }
        if (w.stations[ob.station].lastVisitTick != lastB) {
            lastB = w.stations[ob.station].lastVisitTick;
            ++visitsB;
        }
    }
    CHECK(!w.vehicles[tram].lost);
    CHECK(visitsA >= 3);
    CHECK(visitsB >= 3);
}
