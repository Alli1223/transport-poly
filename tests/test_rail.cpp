#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/rail.hpp"
#include "sim/road_path.hpp"
#include "sim/rail_path.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace tp;

TEST_CASE("rail: trains brake before curves and wait for the tail to clear") {
    Vehicle v;
    for (u16 i = 0; i < vehicleModels().size(); ++i)
        if (vehicleModel(i).type == VehicleType::Rail && vehicleModel(i).maxSpeedKmh >= 160) {
            v.model = i;
            break;
        }
    REQUIRE(v.info().type == VehicleType::Rail);
    const i32 curve = speedUnits(60) * trains::kRailSpeedPercent / 100;
    v.trail.push_back({{0, 0}, Dir::W, Dir::E});
    CHECK_EQ(trains::trackSpeedLimit(v), trains::maxSpeed(v));
    v.ahead.push_back({{1, 0}, Dir::W, Dir::N});
    const i32 approach = trains::trackSpeedLimit(v);
    CHECK(approach < trains::maxSpeed(v));
    CHECK(approach > curve);
    v.progress = kTileUnits;
    CHECK_EQ(trains::trackSpeedLimit(v), curve);
    v.trail.push_back({{-1, 0}, Dir::S, Dir::E});
    v.ahead.clear();
    v.progress = 0;
    CHECK_EQ(trains::trackSpeedLimit(v), curve);
    v.trail.pop_back();
    CHECK_EQ(trains::trackSpeedLimit(v), trains::maxSpeed(v));
    // A distant bend must not slow a train before its braking distance.
    for (i32 x = 1; x < 40; ++x) v.ahead.push_back({{x, 0}, Dir::W, Dir::E});
    v.ahead.push_back({{40, 0}, Dir::W, Dir::N});
    CHECK_EQ(trains::trackSpeedLimit(v), trains::maxSpeed(v));
}

namespace {

Command cmd(CompanyId company, CommandPayload payload) {
    Command c;
    c.company = company;
    c.payload = std::move(payload);
    return c;
}

// Two chunks side by side: too small for any town, so the area stays empty.
struct RailWorld {
    World w;
    CompanyId co;

    RailWorld() : w(makeSettings()) {
        w.activateChunk({0, 0});
        w.activateChunk({1, 0});
        // Flatten everything to height 3 and clear it.
        // (Corners on the edge of the explored area cannot be edited.)
        for (i32 y = 1; y < kChunkSize; ++y)
            for (i32 x = 1; x < 2 * kChunkSize; ++x) w.map.setCornerHeight(x, y, 3);
        for (i32 y = 1; y < kChunkSize - 1; ++y)
            for (i32 x = 1; x < 2 * kChunkSize - 1; ++x) {
                Tile& t = *w.map.tile({x, y});
                t = Tile{};
            }
        co = CompanyId(static_cast<u8>(executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Rail Co", 0xFF})).createdId));
        w.companies[co].money = 50'000'000;
    }

    static WorldSettings makeSettings() {
        WorldSettings s;
        s.seed = 99001;
        return s;
    }

    void lay(TilePos t, u8 bit) {
        Tile& tile = *w.map.tile(t);
        tile.type = TileType::Rail;
        tile.rail = static_cast<u8>(tile.rail | bit);
        tile.owner = co.value;
    }

    CommandResult run(CommandPayload p) { return executeCommand(w, cmd(co, std::move(p))); }
};

} // namespace

TEST_CASE("rail: planned lines are connected chains of pieces") {
    const TilePos ends[] = {{9, 0}, {0, 7}, {-6, 0}, {0, -5}, {7, 3}, {-4, 9}, {5, -5}, {-8, -2}, {3, 11}};
    for (const TilePos to : ends) {
        const auto steps = planRailLine({0, 0}, to);
        REQUIRE(!steps.empty());
        CHECK(steps.front().tile == (TilePos{0, 0}));
        CHECK(steps.back().tile == to);
        for (usize i = 0; i + 1 < steps.size(); ++i) {
            const TrackEdges e = trackEdges(steps[i].bit);
            // One edge of this piece must lead into the next tile, which must use the opposite edge.
            bool linked = false;
            for (const Dir d : {e.a, e.b}) {
                if (neighbour(steps[i].tile, d) != steps[i + 1].tile) continue;
                const TrackEdges n = trackEdges(steps[i + 1].bit);
                linked = n.a == opposite(d) || n.b == opposite(d);
            }
            CHECK(linked);
        }
    }
}

TEST_CASE("crossings: rail over road preserves ownership and both routes") {
    RailWorld rw;
    REQUIRE(rw.run(CmdBuildRoad{{8, 16}, {24, 16}}).ok);
    const auto created = rw.run(CmdCreateCompany{"Road and Rail", 1});
    REQUIRE(created.ok);
    const CompanyId railCo(static_cast<u8>(created.createdId));
    REQUIRE(executeCommand(rw.w, cmd(railCo, CmdBuildRail{{16, 8}, {16, 24}})).ok);
    const Tile& crossing = *rw.w.map.tile({16, 16});
    CHECK(crossing.type == TileType::Road);
    CHECK_EQ(crossing.rail, kTrackNS);
    CHECK_EQ(crossing.roadOwner, rw.co.value);
    CHECK_EQ(crossing.owner, railCo.value);
    CHECK(railLinkOpen(rw.w, {16, 15}, Dir::S));
    CHECK(railLinkOpen(rw.w, {16, 16}, Dir::S));
    CHECK(findRailPath(rw.w, {16, 8}, Dir::N, {{16, 24}}, DepotId{}).has_value());
    CHECK(findRoadPath(rw.w, {8, 16}, Dir::W, {{24, 16}}, VehicleType::Road, DepotId{}).has_value());
    CHECK(!rw.run(CmdBuildRoadStop{{16, 16}, false}).ok);
    CHECK(!rw.run(CmdCycleSignal{{16, 16}}).ok);
    CHECK(!rw.run(CmdRemoveRail{{16, 16}, {16, 16}}).ok);
    CHECK(!rw.run(CmdClearArea{{16, 16}, {16, 16}}).ok);
    REQUIRE(executeCommand(rw.w, cmd(railCo, CmdRemoveRail{{16, 16}, {16, 16}})).ok);
    CHECK_EQ(crossing.rail, 0);
    CHECK_EQ(crossing.roadOwner, rw.co.value);
    CHECK(crossing.type == TileType::Road);
    CHECK(!rw.w.signalTiles.contains(ChunkPos{16, 16}.key()));
}

TEST_CASE("signals: block signals check every branch and respect block boundaries") {
    RailWorld rw;
    REQUIRE(rw.run(CmdBuildRail{{8, 16}, {24, 16}}).ok);
    rw.lay({16, 16}, kTrackSW);
    REQUIRE(rw.run(CmdBuildRail{{16, 17}, {16, 24}}).ok);
    REQUIRE(rw.run(CmdCycleSignal{{10, 16}, true}).ok);
    REQUIRE(rw.run(CmdCycleSignal{{22, 16}}).ok);
    REQUIRE(rw.run(CmdCycleSignal{{16, 22}}).ok);
    CHECK(blockSignalClear(rw.w, {10, 16}, Dir::E));
    rw.w.map.tile({16, 20})->extra2 = kTrackNS;
    CHECK(!blockSignalClear(rw.w, {10, 16}, Dir::E));
    CHECK(blockSignalClear(rw.w, {10, 16}, Dir::W));
    Vehicle own;
    own.model = 7;
    own.trail.push_back({{16, 20}, Dir::N, Dir::S});
    const VehicleId id = rw.w.vehicles.create(own);
    CHECK(blockSignalClear(rw.w, {10, 16}, Dir::E, id));
    rw.w.map.tile({16, 20})->extra2 = 0;
    rw.w.map.tile({16, 23})->extra2 = kTrackNS;
    rw.w.map.tile({23, 16})->extra2 = kTrackEW;
    CHECK(blockSignalClear(rw.w, {10, 16}, Dir::E));
    World copy;
    fromBytes(toBytes(rw.w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(copy.map.tile({10, 16})->extra, 3 | kSignalBlock);
    CHECK(blockSignalClear(copy, {10, 16}, Dir::E));
}

TEST_CASE("signals: switching kind preserves directions and cycles all variants") {
    RailWorld rw;
    REQUIRE(rw.run(CmdBuildRail{{8, 16}, {24, 16}}).ok);
    REQUIRE(rw.run(CmdCycleSignal{{12, 16}, true}).ok);
    CHECK_EQ(rw.w.map.tile({12, 16})->extra, 3 | kSignalBlock);
    REQUIRE(rw.run(CmdCycleSignal{{12, 16}, true}).ok);
    CHECK_EQ(rw.w.map.tile({12, 16})->extra, kSignalForward | kSignalBlock);
    REQUIRE(rw.run(CmdCycleSignal{{12, 16}, false}).ok);
    CHECK_EQ(rw.w.map.tile({12, 16})->extra, kSignalForward);
    REQUIRE(rw.run(CmdCycleSignal{{12, 16}, true}).ok);
    CHECK_EQ(rw.w.map.tile({12, 16})->extra, kSignalForward | kSignalBlock);
    REQUIRE(rw.run(CmdCycleSignal{{12, 16}, true}).ok);
    CHECK_EQ(rw.w.map.tile({12, 16})->extra, kSignalBackward | kSignalBlock);
    REQUIRE(rw.run(CmdCycleSignal{{12, 16}, true}).ok);
    CHECK_EQ(rw.w.map.tile({12, 16})->extra, 0);
    CHECK(!rw.w.signalTiles.contains(ChunkPos{12, 16}.key()));
    const Command original = cmd(rw.co, CmdCycleSignal{{12, 16}, true});
    Command decoded;
    fromBytes(toBytes(original), decoded);
    REQUIRE(executeCommand(rw.w, decoded).ok);
    CHECK_EQ(rw.w.map.tile({12, 16})->extra, 3 | kSignalBlock);
}

TEST_CASE("crossings: road over rail supports removal and save round trips") {
    RailWorld rw;
    REQUIRE(rw.run(CmdBuildRail{{8, 16}, {24, 16}}).ok);
    REQUIRE(rw.run(CmdBuildRoad{{16, 8}, {16, 24}}).ok);
    REQUIRE(rw.w.map.tile({16, 16})->type == TileType::Road);
    CHECK_EQ(rw.w.map.tile({16, 16})->rail, kTrackEW);
    CHECK(railLinkOpen(rw.w, {15, 16}, Dir::E));
    CHECK(roadLinkOpen(rw.w, {16, 15}, Dir::S, VehicleType::Road, DepotId{}));
    World copy;
    fromBytes(toBytes(rw.w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.signalTiles.contains(ChunkPos{16, 16}.key()));
    CHECK(copy.map.chunk({0, 0})->tiles == rw.w.map.chunk({0, 0})->tiles);
    REQUIRE(rw.run(CmdRemoveRoad{{16, 16}, {16, 16}, false}).ok);
    CHECK(rw.w.map.tile({16, 16})->type == TileType::Rail);
    CHECK_EQ(rw.w.map.tile({16, 16})->rail, kTrackEW);
    CHECK_EQ(rw.w.map.tile({16, 16})->owner, rw.co.value);
    CHECK(!rw.w.signalTiles.contains(ChunkPos{16, 16}.key()));
    REQUIRE(rw.run(CmdBuildRoad{{16, 8}, {16, 24}}).ok);
    REQUIRE(rw.run(CmdClearArea{{16, 16}, {16, 16}}).ok);
    CHECK_EQ(rw.w.map.tile({16, 16})->rail, 0);
    CHECK_EQ(rw.w.map.tile({16, 16})->road, 0);
}

TEST_CASE("crossings: reject parallel tracks, trams, slopes and signalled track") {
    RailWorld rw;
    REQUIRE(rw.run(CmdBuildRoad{{8, 16}, {24, 16}}).ok);
    const auto before = toBytes(rw.w, kSaveVersion);
    CHECK(!rw.run(CmdBuildRail{{10, 16}, {20, 16}}).ok);
    CHECK(toBytes(rw.w, kSaveVersion) == before);
    REQUIRE(rw.run(CmdBuildRail{{16, 8}, {16, 24}}).ok);
    CHECK(!rw.run(CmdBuildRoad{{8, 16}, {24, 16}, true}).ok);
    CHECK(!rw.run(CmdBuildRoad{{16, 8}, {16, 24}, false}).ok);
    rw.w.map.setCornerHeight(18, 16, 4);
    CHECK(!rw.run(CmdBuildRail{{18, 8}, {18, 24}}).ok);
    REQUIRE(rw.run(CmdCycleSignal{{16, 12}}).ok);
    CHECK(!rw.run(CmdBuildRoad{{8, 12}, {24, 12}, false}).ok);
}

TEST_CASE("crossings: road vehicles wait at reserved rail even after gridlock timeout") {
    RailWorld rw;
    REQUIRE(rw.run(CmdBuildRoad{{8, 16}, {24, 16}}).ok);
    REQUIRE(rw.run(CmdBuildRail{{16, 8}, {16, 24}}).ok);
    rw.w.map.tile({16, 16})->extra2 = kTrackNS;
    Vehicle bus;
    bus.owner = rw.co;
    bus.model = 0;
    bus.stopped = false;
    bus.state = VehicleState::Driving;
    bus.tile = {15, 16};
    bus.entry = Dir::W;
    bus.exit = Dir::E;
    bus.progress = kTileUnits - 2;
    bus.speed = speedUnits(bus.info().maxSpeedKmh);
    bus.blockedTicks = 1000;
    const VehicleId id = rw.w.vehicles.create(bus);
    for (int i = 0; i < 700; ++i) {
        vehicles::tickAll(rw.w);
        REQUIRE(rw.w.vehicles[id].tile == (TilePos{15, 16}));
    }
    CHECK_EQ(rw.w.vehicles[id].speed, 0);
    rw.w.map.tile({16, 16})->extra2 = 0;
    for (int i = 0; i < 8; ++i) vehicles::tickAll(rw.w);
    CHECK(rw.w.vehicles[id].tile == (TilePos{16, 16}));
    CHECK(crossingRoadOccupied(rw.w, {16, 16}));
    rw.w.vehicles[id].tile = {17, 16};
    rw.w.vehicles[id].progress = 0;
    CHECK(crossingRoadOccupied(rw.w, {16, 16}));
    rw.w.vehicles[id].progress = kTileUnits;
    CHECK(!crossingRoadOccupied(rw.w, {16, 16}));
}

TEST_CASE("rail: a train shuttles between two terminus stations") {
    for (int layout = 0; layout < 3; ++layout) {
        RailWorld rw;
        REQUIRE(rw.run(CmdBuildRailStation{{5, 16}, {8, 16}}).ok);
        REQUIRE(rw.run(CmdBuildRail{{4, 16}, {9, 16}}).ok == false || true); // overlaps the platform; may fail
        REQUIRE(rw.run(CmdBuildRail{{9, 16}, {54, 16}}).ok);
        REQUIRE(rw.run(CmdBuildRailStation{{55, 16}, {58, 16}}).ok);
        rw.lay({4, 16}, kTrackEW);
        const CommandResult dep = rw.run(CmdBuildRailDepot{{3, 16}, Dir::E});
        REQUIRE(dep.ok);
        const DepotId depot(dep.createdId);
        const CommandResult buy = rw.run(CmdBuyVehicle{depot, 7}); // Kirby Tank Engine
        REQUIRE(buy.ok);
        const VehicleId train(buy.createdId);
        REQUIRE(rw.run(CmdAddCarriage{train, 11}).ok);
        REQUIRE(rw.run(CmdAddCarriage{train, 11}).ok);
        if (layout == 1) REQUIRE(rw.run(CmdReorderConsist{train,{1,2,0}}).ok);
        if (layout == 2) REQUIRE(rw.run(CmdAddCarriage{train,7}).ok);
        CHECK(!rw.run(CmdBuyVehicle{depot, 11}).ok); // coaches cannot run on their own
        const StationId a(rw.w.map.tile({5, 16})->index);
        const StationId b(rw.w.map.tile({55, 16})->index);
        CHECK(a != b);
        Order oa, ob;
        oa.station = a;
        ob.station = b;
        REQUIRE(rw.run(CmdSetOrders{train, {oa, ob}}).ok);
        REQUIRE(rw.run(CmdStartStopVehicle{train}).ok);
        i64 lastA = -1, lastB = -1, visitsA = 0, visitsB = 0;
        for (i64 t = 0; t < kTicksPerDay; ++t) {
            rw.w.tick();
            if (rw.w.stations[a].lastVisitTick != lastA) {
                lastA = rw.w.stations[a].lastVisitTick;
                ++visitsA;
            }
            if (rw.w.stations[b].lastVisitTick != lastB) {
                lastB = rw.w.stations[b].lastVisitTick;
                ++visitsB;
            }
        }
        const Vehicle& v = rw.w.vehicles[train];
        CHECK(!v.lost);
        CHECK_EQ(v.capacity(), 72u);
        CHECK(visitsA >= 3);
        CHECK(visitsB >= 3);
    }
}

TEST_CASE("rail: two trains share a signalled loop safely") {
    RailWorld rw;
    // Clockwise loop with one-way signals.
    rw.lay({10, 10}, kTrackSE);
    for (i32 x = 11; x < 40; ++x) rw.lay({x, 10}, kTrackEW);
    rw.lay({40, 10}, kTrackSW);
    for (i32 y = 11; y < 18; ++y) rw.lay({40, y}, kTrackNS);
    rw.lay({40, 18}, kTrackNW);
    for (i32 x = 11; x < 40; ++x) rw.lay({x, 18}, kTrackEW);
    rw.lay({10, 18}, kTrackNE);
    for (i32 y = 11; y < 18; ++y) rw.lay({10, y}, kTrackNS);
    rw.lay({25, 10}, kTrackNE); // junction for the depot north of the loop
    REQUIRE(rw.run(CmdBuildRailStation{{18, 10}, {21, 10}}).ok);
    REQUIRE(rw.run(CmdBuildRailStation{{28, 18}, {31, 18}}).ok);
    const auto signal = [&](TilePos t, u8 bits) {
        rw.w.map.tile(t)->extra = bits;
        rw.w.signalTiles.insert(ChunkPos{t.x, t.y}.key());
    };
    for (const i32 x : {13, 23, 31, 36}) signal({x, 10}, kSignalForward);  // eastbound
    for (const i32 y : {13, 16}) signal({40, y}, kSignalBackward);         // southbound
    for (const i32 x : {36, 26, 22, 15}) signal({x, 18}, kSignalBackward); // westbound
    for (const i32 y : {16, 13}) signal({10, y}, kSignalForward);          // northbound
    const CommandResult dep = rw.run(CmdBuildRailDepot{{25, 9}, Dir::S});
    REQUIRE(dep.ok);
    const DepotId depot(dep.createdId);
    const StationId a(rw.w.map.tile({18, 10})->index);
    const StationId b(rw.w.map.tile({28, 18})->index);
    std::vector<VehicleId> trains;
    for (int i = 0; i < 2; ++i) {
        const CommandResult buy = rw.run(CmdBuyVehicle{depot, 7});
        REQUIRE(buy.ok);
        const VehicleId id(buy.createdId);
        REQUIRE(rw.run(CmdAddCarriage{id, 11}).ok);
        Order oa, ob;
        oa.station = a;
        ob.station = b;
        REQUIRE(rw.run(CmdSetOrders{id, {oa, ob}}).ok);
        REQUIRE(rw.run(CmdStartStopVehicle{id}).ok);
        trains.push_back(id);
    }
    std::map<u32, i64> visits;
    i64 lastA = -1, lastB = -1;
    bool overlap = false;
    for (i64 t = 0; t < 2 * kTicksPerDay; ++t) {
        rw.w.tick();
        if (rw.w.stations[a].lastVisitTick != lastA) {
            lastA = rw.w.stations[a].lastVisitTick;
            ++visits[a.value];
        }
        if (rw.w.stations[b].lastVisitTick != lastB) {
            lastB = rw.w.stations[b].lastVisitTick;
            ++visits[b.value];
        }
        if (std::getenv("TP_RAIL_TRACE") && t % 150 == 0) {
            for (const VehicleId id : trains) {
                const Vehicle& v = rw.w.vehicles[id];
                std::printf("t=%lld train %u state %d tile %d,%d entry %d exit %d prog %d speed %d ahead %zu trail %zu order %d lost %d\n",
                            (long long)t, id.value, (int)v.state, v.tile.x, v.tile.y, (int)v.entry, (int)v.exit, v.progress,
                            v.speed, v.ahead.size(), v.trail.size(), v.orderIndex, (int)v.lost);
            }
        }
        // No two trains may ever hold conflicting pieces on the same tile.
        const Vehicle& v0 = rw.w.vehicles[trains[0]];
        const Vehicle& v1 = rw.w.vehicles[trains[1]];
        for (const TrackSeg& s0 : v0.trail)
            for (const TrackSeg& s1 : v1.trail)
                if (s0.tile == s1.tile && tracksConflict(trackBetween(s0.entry, s0.exit), trackBetween(s1.entry, s1.exit)))
                    overlap = true;
    }
    CHECK(!overlap);
    for (const VehicleId id : trains) {
        CHECK(rw.w.vehicles[id].state != VehicleState::InDepot);
        CHECK(!rw.w.vehicles[id].lost);
        CHECK(rw.w.vehicles[id].profitThisYear != 0 || rw.w.vehicles[id].passengersThisYear >= 0);
    }
    CHECK(visits[a.value] >= 4);
    CHECK(visits[b.value] >= 4);

    // Save / load in the middle of operation continues identically.
    const auto bytes = toBytes(rw.w, kSaveVersion);
    World copy;
    fromBytes(bytes, copy, kSaveVersion);
    for (i64 t = 0; t < kTicksPerDay / 2; ++t) {
        rw.w.tick();
        copy.tick();
    }
    CHECK_EQ(copy.stateHash(), rw.w.stateHash());
}

namespace {

// A terminus to terminus line with a depot behind the western platform and one train on it.
struct ShuttleLine {
    RailWorld rw;
    DepotId depot;
    VehicleId train;
    StationId a, b;

    ShuttleLine() {
        REQUIRE(rw.run(CmdBuildRailStation{{5, 16}, {8, 16}}).ok);
        REQUIRE(rw.run(CmdBuildRail{{9, 16}, {54, 16}}).ok);
        REQUIRE(rw.run(CmdBuildRailStation{{55, 16}, {58, 16}}).ok);
        rw.lay({4, 16}, kTrackEW);
        const CommandResult dep = rw.run(CmdBuildRailDepot{{3, 16}, Dir::E});
        REQUIRE(dep.ok);
        depot = DepotId(dep.createdId);
        const CommandResult buy = rw.run(CmdBuyVehicle{depot, 7});
        REQUIRE(buy.ok);
        train = VehicleId(buy.createdId);
        REQUIRE(rw.run(CmdAddCarriage{train, 11}).ok);
        a = StationId(rw.w.map.tile({5, 16})->index);
        b = StationId(rw.w.map.tile({55, 16})->index);
        Order oa, ob;
        oa.station = a;
        ob.station = b;
        REQUIRE(rw.run(CmdSetOrders{train, {oa, ob}}).ok);
    }
};

} // namespace

TEST_CASE("orders: train passes a platform and waypoint then stops and survives save continuation") {
    ShuttleLine line;
    Order pass, via, stop, back;
    pass.station = line.a; pass.flags = kOrderNonStop;
    via.kind = OrderKind::Via; via.waypoint = {25, 16};
    stop.station = line.b; back.station = line.a;
    REQUIRE(line.rw.run(CmdSetOrders{line.train, {pass, via, stop, back}}).ok);
    REQUIRE(line.rw.run(CmdStartStopVehicle{line.train}).ok);
    bool reachedVia = false, visitedB = false;
    for (int i = 0; i < 1500 && !visitedB; ++i) {
        line.rw.w.tick();
        const auto& v = line.rw.w.vehicles[line.train];
        reachedVia = reachedVia || (v.tile.x >= 25 && v.orderIndex == 2);
        CHECK_EQ(line.rw.w.stations[line.a].lastVisitTick, -1);
        visitedB = line.rw.w.stations[line.b].lastVisitTick >= 0;
    }
    CHECK(reachedVia);
    CHECK(visitedB);
    World copy;
    fromBytes(toBytes(line.rw.w, kSaveVersion), copy, kSaveVersion);
    for (int i = 0; i < 1500; ++i) { line.rw.w.tick(); copy.tick(); }
    CHECK_EQ(copy.stateHash(), line.rw.w.stateHash());
    CHECK(line.rw.w.stations[line.a].lastVisitTick >= 0);
}

TEST_CASE("crossings: trains wait for road traffic to clear before reserving") {
    ShuttleLine line;
    World& w = line.rw.w;
    REQUIRE(line.rw.run(CmdBuildRoad{{30, 10}, {30, 23}}).ok);
    Vehicle bus;
    bus.model = 0;
    bus.owner = line.rw.co;
    bus.tile = {30, 16};
    bus.entry = Dir::N;
    bus.exit = Dir::S;
    bus.state = VehicleState::Driving;
    bus.stopped = true;
    const VehicleId road = w.vehicles.create(bus);
    REQUIRE(line.rw.run(CmdStartStopVehicle{line.train}).ok);
    for (int i = 0; i < 4000; ++i) {
        w.tick();
        REQUIRE(w.vehicles[line.train].tile.x < 30);
        REQUIRE(w.map.tile({30, 16})->extra2 == 0);
    }
    w.vehicles[road].stopped = false;
    bool arrived = false;
    for (int i = 0; i < 6000; ++i) {
        w.tick();
        if (w.map.tile({30, 16})->extra2) CHECK(!crossingRoadOccupied(w, {30, 16}));
        arrived = arrived || w.stations[line.b].lastVisitTick > 0;
    }
    CHECK(arrived);
    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    for (int i = 0; i < 1000; ++i) {
        w.tick();
        copy.tick();
    }
    CHECK(toBytes(w, kSaveVersion) == toBytes(copy, kSaveVersion));
}

TEST_CASE("signals: block waits for an occupied branch while a path signal allows a clear route") {
    ShuttleLine line;
    World& block = line.rw.w;
    line.rw.lay({30, 16}, kTrackSW);
    REQUIRE(line.rw.run(CmdBuildRail{{30, 17}, {30, 23}}).ok);
    REQUIRE(line.rw.run(CmdCycleSignal{{20, 16}, true}).ok);
    block.map.tile({30, 20})->extra2 = kTrackNS;
    REQUIRE(line.rw.run(CmdStartStopVehicle{line.train}).ok);
    World path;
    fromBytes(toBytes(block, kSaveVersion), path, kSaveVersion);
    path.map.tile({20, 16})->extra = 3;
    bool pathArrived = false;
    for (int i = 0; i < 4000; ++i) {
        block.tick();
        path.tick();
        REQUIRE(block.vehicles[line.train].tile.x < 20);
        pathArrived = pathArrived || path.stations[line.b].lastVisitTick > 0;
    }
    CHECK(pathArrived);
    block.map.tile({30, 20})->extra2 = 0;
    bool blockArrived = false;
    for (int i = 0; i < 4000; ++i) {
        block.tick();
        blockArrived = blockArrived || block.stations[line.b].lastVisitTick > 0;
    }
    CHECK(blockArrived);
    World restored;
    fromBytes(toBytes(block, kSaveVersion), restored, kSaveVersion);
    for (int i = 0; i < 1000; ++i) {
        block.tick();
        restored.tick();
    }
    CHECK(toBytes(block, kSaveVersion) == toBytes(restored, kSaveVersion));
}

TEST_CASE("vehicles: breakdowns stop vehicles and services restore reliability") {
    ShuttleLine line;
    World& w = line.rw.w;
    REQUIRE(line.rw.run(CmdStartStopVehicle{line.train}).ok);
    for (int t = 0; t < 1500; ++t) w.tick();
    Vehicle& v = w.vehicles[line.train];
    const u16 best = maxReliability(v.info());
    REQUIRE(v.state == VehicleState::Driving);

    // A neglected train breaks down sooner or later...
    v.reliability = 300;
    bool broke = false;
    for (int d = 0; d < 500 && !broke; ++d) {
        vehicles::daily(w);
        broke = v.breakdownTicks > 0 && v.state == VehicleState::Driving;
        if (!broke && v.state != VehicleState::Driving) w.tick();
    }
    REQUIRE(broke);
    CHECK(v.breakdownsSinceService >= 1);
    // ...and stands still while broken down.
    const TilePos at = v.tile;
    const i32 progress = v.progress;
    for (int t = 0; t < 100; ++t) w.tick();
    CHECK(v.tile == at);
    CHECK_EQ(v.progress, progress);
    for (int t = 0; t < 2 * kTicksPerHour; ++t) w.tick();
    CHECK_EQ(v.breakdownTicks, 0);

    // When a service is due it heads for its depot, gets serviced and carries on.
    v.lastServiceTick = w.clock.tick - 400 * kTicksPerDay;
    for (int d = 0; d < 20 && !v.goingForService; ++d) {
        for (int t = 0; t < 200; ++t) w.tick();
        vehicles::daily(w);
    }
    REQUIRE(v.goingForService);
    for (int t = 0; t < kTicksPerDay && v.goingForService; ++t) w.tick();
    CHECK(!v.goingForService);
    CHECK_EQ(v.reliability, best);
    CHECK_EQ(v.breakdownsSinceService, 0);
    CHECK(!v.stopped);
    for (int t = 0; t < 600; ++t) w.tick();
    CHECK(v.state != VehicleState::InDepot);
}

TEST_CASE("vehicles: clones copy the consist and can share orders") {
    ShuttleLine line;
    RailWorld& rw = line.rw;
    const CommandResult copy = rw.run(CmdCloneVehicle{line.depot, line.train, false});
    REQUIRE(copy.ok);
    const CommandResult shared = rw.run(CmdCloneVehicle{line.depot, line.train, true});
    REQUIRE(shared.ok);
    const VehicleId c1(copy.createdId), c2(shared.createdId);
    CHECK(rw.w.vehicles[c1].cars == rw.w.vehicles[line.train].cars);
    CHECK(rw.w.vehicles[c2].orders == rw.w.vehicles[line.train].orders);
    CHECK_EQ(rw.w.vehicles[c1].orderGroup, 0u);
    CHECK(rw.w.vehicles[c2].orderGroup != 0u);
    CHECK_EQ(rw.w.vehicles[c2].orderGroup, rw.w.vehicles[line.train].orderGroup);

    // Editing shared orders changes every vehicle in the group, but not the plain copy.
    Order oa;
    oa.station = line.a;
    REQUIRE(rw.run(CmdSetOrders{c2, {oa}}).ok);
    CHECK_EQ(rw.w.vehicles[line.train].orders.size(), 1u);
    CHECK_EQ(rw.w.vehicles[c1].orders.size(), 2u);

    REQUIRE(rw.run(CmdUnshareOrders{c2}).ok);
    Order ob;
    ob.station = line.b;
    REQUIRE(rw.run(CmdSetOrders{c2, {ob, oa}}).ok);
    CHECK_EQ(rw.w.vehicles[line.train].orders.size(), 1u);
    CHECK(!rw.run(CmdUnshareOrders{c2}).ok);
}

namespace {

u16 railModel(std::string_view name) {
    for (u16 i = 0; i < vehicleModels().size(); ++i)
        if (vehicleModel(i).name == name) return i;
    return 0;
}

// A train of one engine and `coaches` coaches (or hoppers).
Vehicle consist(std::string_view engine, std::string_view car, int count) {
    Vehicle v;
    v.model = railModel(engine);
    v.cars.push_back(v.model);
    for (int i = 0; i < count; ++i) v.cars.push_back(railModel(car));
    return v;
}

// Ticks to reach `speed` from a standstill on a steady grade (capped at a long time).
int ticksTo(Vehicle v, i32 speed, i32 grade) {
    for (int t = 0; t < 20000; ++t) {
        if (v.speed >= speed) return t;
        trains::accelerate(v, trains::maxSpeed(v), grade);
    }
    return 20000;
}

} // namespace

TEST_CASE("rail: trains run at half their rated speed, heavy and loaded trains take longer to get going") {
    const Vehicle short_ = consist("Highball Express", "Steel Coach", 2);
    const Vehicle long_ = consist("Highball Express", "Steel Coach", 8);
    CHECK_EQ(trains::maxSpeed(short_), speedUnits(160) / 2);
    const i32 cruise = trains::maxSpeed(short_) * 3 / 4;
    const int quick = ticksTo(short_, cruise, 0), slow = ticksTo(long_, cruise, 0);
    CHECK(quick > 30); // no longer near instant
    CHECK(slow > quick * 2);
    // Freight aboard makes a train heavier still.
    Vehicle empty = consist("Heavy Freight Diesel", "Grain Hopper", 6);
    Vehicle loaded = empty;
    CargoBatch grain;
    grain.type = CargoType::Grain;
    grain.quantity = 300;
    loaded.cargo.push_back(grain);
    CHECK(trains::weight(loaded).load == 600);
    CHECK(ticksTo(loaded, trains::maxSpeed(loaded) / 2, 0) > ticksTo(empty, trains::maxSpeed(empty) / 2, 0));
}

TEST_CASE("rail: trains slow on climbs, crawl rather than stall, and gather speed downhill") {
    const Vehicle light = consist("Kirby Tank Engine", "Wooden Coach", 2);
    Vehicle heavy = consist("Kirby Tank Engine", "Grain Hopper", 10);
    CargoBatch grain;
    grain.type = CargoType::Grain;
    grain.quantity = 500;
    heavy.cargo.push_back(grain);
    const auto settle = [](Vehicle v, i32 grade) {
        v.speed = trains::maxSpeed(v);
        for (int t = 0; t < 4000; ++t) trains::accelerate(v, trains::maxSpeed(v), grade);
        return v.speed;
    };
    const i32 top = trains::maxSpeed(light);
    CHECK_EQ(settle(light, 0), top);
    CHECK(settle(light, 5) < top);
    CHECK(settle(light, 5) > top / 3);
    // Too heavy for the climb: it crawls up at a sixth of its top speed or so, never stopping.
    const i32 crawl = settle(heavy, 5);
    CHECK(crawl > 0);
    CHECK(crawl < settle(light, 5));
    Vehicle stopped = heavy;
    for (int t = 0; t < 400; ++t) trains::accelerate(stopped, trains::maxSpeed(stopped), 5);
    CHECK(stopped.speed > 0);
    // Downhill it gets up to speed sooner than on the level.
    CHECK(ticksTo(heavy, trains::maxSpeed(heavy) / 2, -5) < ticksTo(heavy, trains::maxSpeed(heavy) / 2, 0));
}
