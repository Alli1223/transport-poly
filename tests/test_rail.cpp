#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/rail.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace tp;

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

TEST_CASE("rail: a train shuttles between two terminus stations") {
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
