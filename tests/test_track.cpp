#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/passengers.hpp"
#include "sim/town.hpp"
#include "sim/track.hpp"
#include "sim/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace tp;

namespace {

Command cmd(CompanyId company, CommandPayload payload) {
    Command c;
    c.company = company;
    c.payload = std::move(payload);
    return c;
}

// Four chunks of empty land at height 4 (no towns that small).
struct TrackWorld {
    World w;
    CompanyId co;

    TrackWorld() : w(makeSettings()) {
        for (i32 y = 0; y < 2; ++y)
            for (i32 x = 0; x < 2; ++x) w.activateChunk({x, y});
        for (i32 y = 1; y < 2 * kChunkSize; ++y)
            for (i32 x = 1; x < 2 * kChunkSize; ++x) w.map.setCornerFine(x, y, 4 * kSubLevels);
        for (i32 y = 0; y < 2 * kChunkSize; ++y)
            for (i32 x = 0; x < 2 * kChunkSize; ++x) *w.map.tile({x, y}) = Tile{};
        co = CompanyId(static_cast<u8>(executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Track Co", 0xFF})).createdId));
        w.companies[co].money = 50'000'000;
    }

    static WorldSettings makeSettings() {
        WorldSettings s;
        s.seed = 4242;
        return s;
    }

    CommandResult run(CommandPayload p) { return executeCommand(w, cmd(co, std::move(p))); }
};

TPoint at(double tx, double ty) {
    return {static_cast<i32>(tx * kTrackUnits), static_cast<i32>(ty * kTrackUnits)};
}

TrackAnchor freeAnchor(TPoint p, TDir into = {0, 0}) {
    TrackAnchor a;
    a.pos = p;
    a.into = into;
    return a;
}

} // namespace

TEST_CASE("track: straight track lays a bed and chains on from its end") {
    TrackWorld tw;
    const CommandResult r = tw.run(CmdBuildTrack{freeAnchor(at(5, 10)), freeAnchor(at(25, 10))});
    REQUIRE(r.ok);
    CHECK(r.cost > 0);
    // 20 tiles are split into pieces of at most 8 tiles.
    CHECK_EQ(tw.w.trackEdges.size(), 3u);
    CHECK_EQ(tw.w.trackNodes.size(), 4u);
    i64 length = 0;
    for (const auto [id, e] : tw.w.trackEdges) length += e.length;
    CHECK(std::abs(length - 20 * kTrackUnits) < 8);
    CHECK(tw.w.map.tile({15, 10})->type == TileType::Track);
    CHECK(tw.w.map.tile({15, 14})->type == TileType::Clear);

    // Continuing from the end point (as the build tool does) joins the existing end node.
    const TrackNodeId end = track::nodeNear(tw.w, at(25, 10), 32);
    REQUIRE(end.valid());
    CHECK(tw.run(CmdBuildTrack{freeAnchor(at(25, 10), TDir{kDirOne, 0}), freeAnchor(at(35, 14))}).ok);
    const TrackNode& n = tw.w.trackNodes[end];
    CHECK_EQ(n.count(0) + n.count(1), 2);
    CHECK(n.count(0) == 1 && n.count(1) == 1);
}

TEST_CASE("track: curves keep to the minimum radius and cannot double back") {
    TrackWorld tw;
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(5, 30)), freeAnchor(at(15, 30))}).ok);
    // A gentle curve continuing east is fine...
    const TrackPlan gentle = track::plan(tw.w, tw.co, freeAnchor(at(15, 30), TDir{kDirOne, 0}), freeAnchor(at(35, 38)));
    REQUIRE(gentle.ok);
    CHECK(gentle.minRadius >= kTrackMinRadius);
    // ...but a sharp one is not, nor is turning back on itself.
    const TrackPlan sharp = track::plan(tw.w, tw.co, freeAnchor(at(15, 30), TDir{kDirOne, 0}), freeAnchor(at(17, 33)));
    CHECK(!sharp.ok);
    const TrackPlan back = track::plan(tw.w, tw.co, freeAnchor(at(15, 30), TDir{kDirOne, 0}), freeAnchor(at(8, 31)));
    CHECK(!back.ok);
}

TEST_CASE("track: the terrain is shaped into a smooth bed with embankments and cuttings") {
    TrackWorld tw;
    // A ridge across the route and a dip after it (not so high or deep to need a tunnel or bridge).
    for (i32 y = 1; y < 63; ++y)
        for (i32 x = 1; x < 63; ++x) {
            if (x >= 20 && x <= 26) tw.w.map.setCornerFine(x, y, 6 * kSubLevels);
            if (x >= 34 && x <= 40) tw.w.map.setCornerFine(x, y, 2 * kSubLevels);
        }
    const CommandResult r = tw.run(CmdBuildTrack{freeAnchor(at(4, 32)), freeAnchor(at(52, 32))});
    REQUIRE(r.ok);
    // Along the centre line the ground sits just under the rails...
    for (const auto [id, e] : tw.w.trackEdges)
        for (const TrackSample& s : e.samples) {
            const i32 ground = track::terrainAt(tw.w, s.p);
            CHECK(std::abs(ground - (s.h - 5)) <= 2);
        }
    // ...the ridge has been cut and the dip filled, while land well away is untouched.
    CHECK(tw.w.map.cornerFine(23, 32) < 6 * kSubLevels);
    CHECK(tw.w.map.cornerFine(37, 32) > 2 * kSubLevels);
    CHECK_EQ(tw.w.map.cornerFine(23, 50), 6 * kSubLevels);
    CHECK_EQ(tw.w.map.cornerFine(37, 10), 2 * kSubLevels);
    for (const auto [id, e] : tw.w.trackEdges) CHECK(e.spans.empty());
    // The slope eases out: the cutting's sides are not vertical.
    CHECK(tw.w.map.cornerFine(23, 34) > tw.w.map.cornerFine(23, 32));
    CHECK(tw.w.map.cornerFine(23, 34) < 6 * kSubLevels);
}

TEST_CASE("track: gradients are limited") {
    TrackWorld tw;
    for (i32 y = 1; y < 63; ++y)
        for (i32 x = 30; x < 63; ++x) tw.w.map.setCornerFine(x, y, 30 * kSubLevels);
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(4, 20)), freeAnchor(at(20, 20))}).ok);
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(40, 20)), freeAnchor(at(56, 20))}).ok);
    const TrackNodeId low = track::nodeNear(tw.w, at(20, 20), 32);
    const TrackNodeId high = track::nodeNear(tw.w, at(40, 20), 32);
    REQUIRE(low.valid());
    REQUIRE(high.valid());
    TrackAnchor a;
    a.node = low;
    TrackAnchor b;
    b.node = high;
    const TrackPlan p = track::plan(tw.w, tw.co, a, b);
    CHECK(!p.ok);
    // Free ends never exceed the limit: the end height is pulled towards the start.
    const TrackPlan up = track::plan(tw.w, tw.co, freeAnchor(at(20, 40)), freeAnchor(at(36, 40)));
    REQUIRE(up.ok);
    CHECK(up.samples.back().h - up.samples.front().h <= kTrackMaxGrade * 16 + 1);
}

TEST_CASE("track: branching makes a switch, removal tidies up, saves keep the network") {
    TrackWorld tw;
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(5, 12)), freeAnchor(at(21, 12))}).ok);
    // Branch from the joint node in the middle, heading the same way.
    const TrackNodeId mid = track::nodeNear(tw.w, at(13, 12), 32, true);
    REQUIRE(mid.valid());
    TrackAnchor from;
    from.node = mid;
    from.into = TDir{kDirOne, 0};
    REQUIRE(tw.run(CmdBuildTrack{from, freeAnchor(at(27, 20))}).ok);
    const TrackNode& m = tw.w.trackNodes[mid];
    CHECK(m.count(0) == 2 || m.count(1) == 2);

    // Save / load round trip keeps the network and rebuilds its samples.
    const auto bytes = toBytes(tw.w, kSaveVersion);
    World copy;
    fromBytes(bytes, copy, kSaveVersion);
    CHECK_EQ(copy.trackEdges.size(), tw.w.trackEdges.size());
    for (const auto [id, e] : tw.w.trackEdges) {
        REQUIRE(copy.trackEdges.contains(id));
        CHECK_EQ(copy.trackEdges[id].length, e.length);
    }
    CHECK_EQ(copy.stateHash(), tw.w.stateHash());

    // Remove everything: tiles return to clear land and no nodes are left behind.
    std::vector<TrackEdgeId> ids;
    for (const auto [id, e] : tw.w.trackEdges) ids.push_back(id);
    for (const TrackEdgeId id : ids) CHECK(tw.run(CmdRemoveTrack{id}).ok);
    CHECK_EQ(tw.w.trackNodes.size(), 0u);
    CHECK(tw.w.map.tile({13, 12})->type == TileType::Clear);
    CHECK(tw.w.trackTiles.empty());
}

TEST_CASE("track: obstacles under the bed are refused") {
    TrackWorld tw;
    tw.w.map.tile({15, 25})->type = TileType::Water;
    const CommandResult r = tw.run(CmdBuildTrack{freeAnchor(at(5, 25)), freeAnchor(at(25, 25))});
    CHECK(!r.ok);
    CHECK(r.error.find("water") != std::string::npos);
}

namespace {

TrackAnchor nodeAnchor(const World& w, TPoint p, TDir into = {0, 0}) {
    TrackAnchor a;
    a.node = track::nodeNear(w, p, 40, true);
    a.pos = p;
    a.into = into;
    return a;
}

StationId stationAt(const World& w, TPoint p) {
    for (const auto [id, e] : w.trackEdges)
        if (e.kind == TrackKind::Platform && e.samples.size() > 1) {
            const TrackSample mid = track::pointOn(e, true, e.length / 2);
            if (distance(mid.p, p) < 4 * kTrackUnits) return e.station;
        }
    return {};
}

} // namespace

TEST_CASE("track: a train shuttles between two stations on free track") {
    for (int layout = 0; layout < 3; ++layout) {
        TrackWorld tw;
        // Station A, track east, station B; a depot behind A.
        REQUIRE(tw.run(CmdBuildTrackStation{freeAnchor(at(8, 16)), at(14, 16)}).ok);
        REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(14, 16), TDir{kDirOne, 0}), freeAnchor(at(40, 18))}).ok);
        const TrackNodeId bEnd = track::nodeNear(tw.w, at(40, 18), 40);
        REQUIRE(bEnd.valid());
        TrackAnchor bStart;
        bStart.node = bEnd;
        REQUIRE(tw.run(CmdBuildTrackStation{bStart, at(46, 20)}).ok);
        const CommandResult dep = tw.run(CmdBuildTrackDepot{track::nodeNear(tw.w, at(8, 16), 40)});
        REQUIRE(dep.ok);
        const DepotId depot(dep.createdId);
        const StationId a = stationAt(tw.w, at(11, 16));
        const StationId b = stationAt(tw.w, at(43, 19));
        REQUIRE(a.valid());
        REQUIRE(b.valid());
        CHECK(a != b);

        const CommandResult buy = tw.run(CmdBuyVehicle{depot, 7});
        REQUIRE(buy.ok);
        const VehicleId train(buy.createdId);
        CHECK(tw.w.vehicles[train].freeTrack);
        REQUIRE(tw.run(CmdAddCarriage{train, 11}).ok);
        if (layout == 1) REQUIRE(tw.run(CmdReorderConsist{train,{1,0}}).ok);
        if (layout == 2) REQUIRE(tw.run(CmdAddCarriage{train,7}).ok);
        Order oa, ob;
        oa.station = a;
        ob.station = b;
        REQUIRE(tw.run(CmdSetOrders{train, {oa, ob}}).ok);
        REQUIRE(tw.run(CmdStartStopVehicle{train}).ok);
        i64 lastA = -1, lastB = -1, visitsA = 0, visitsB = 0;
        for (i64 t = 0; t < kTicksPerDay; ++t) {
            tw.w.tick();
            if (tw.w.stations[a].lastVisitTick != lastA) {
                lastA = tw.w.stations[a].lastVisitTick;
                ++visitsA;
            }
            if (tw.w.stations[b].lastVisitTick != lastB) {
                lastB = tw.w.stations[b].lastVisitTick;
                ++visitsB;
            }
        }
        const Vehicle& v = tw.w.vehicles[train];
        CHECK(!v.lost);
        CHECK(v.state != VehicleState::InDepot);
        CHECK(visitsA >= 3);
        CHECK(visitsB >= 3);
        // Track a train holds cannot be pulled up.
        bool held = false;
        for (const auto [id, e] : tw.w.trackEdges)
            if (e.reservedBy == train) held = held || !tw.run(CmdRemoveTrack{id}).ok;
        CHECK(held);

        // Send it home: it drives into the depot and stops there.
        REQUIRE(tw.run(CmdSendToDepot{train}).ok);
        for (i64 t = 0; t < kTicksPerDay && tw.w.vehicles[train].state != VehicleState::InDepot; ++t) tw.w.tick();
        CHECK(tw.w.vehicles[train].state == VehicleState::InDepot);
        for (const auto [id, e] : tw.w.trackEdges) CHECK(!e.reservedBy.valid());
    }
}

TEST_CASE("track: two trains share a signalled loop safely, and saves continue identically") {
    TrackWorld tw;
    // An oval: top straight with station A, curves at both ends, bottom straight with station B.
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(10, 10)), freeAnchor(at(18, 10))}).ok);
    REQUIRE(tw.run(CmdBuildTrackStation{nodeAnchor(tw.w, at(18, 10)), at(24, 10)}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(24, 10), TDir{kDirOne, 0}), freeAnchor(at(40, 10))}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(40, 10), TDir{kDirOne, 0}), freeAnchor(at(48, 18))}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(48, 18), TDir{0, kDirOne}), freeAnchor(at(40, 26))}).ok);
    REQUIRE(tw.run(CmdBuildTrackStation{nodeAnchor(tw.w, at(40, 26)), at(34, 26)}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(34, 26), TDir{-kDirOne, 0}), freeAnchor(at(10, 26))}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(10, 26), TDir{-kDirOne, 0}), freeAnchor(at(2, 18))}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(2, 18), TDir{0, -kDirOne}), nodeAnchor(tw.w, at(10, 10), TDir{-kDirOne, 0})}).ok);
    // A spur off the top straight for the depot.
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(32, 10), TDir{kDirOne, 0}), freeAnchor(at(41, 4))}).ok);
    const CommandResult dep = tw.run(CmdBuildTrackDepot{track::nodeNear(tw.w, at(41, 4), 40)});
    REQUIRE(dep.ok);
    const DepotId depot(dep.createdId);
    // Two-way signals at a few nodes around the loop.
    int signals = 0;
    for (const TPoint p : {at(40, 10), at(48, 18), at(10, 26), at(2, 18)}) {
        const TrackNodeId n = track::nodeNear(tw.w, p, 40, true);
        if (n.valid() && tw.run(CmdTrackSignal{n}).ok) ++signals;
    }
    CHECK(signals >= 3);
    const StationId a = stationAt(tw.w, at(21, 10));
    const StationId b = stationAt(tw.w, at(37, 26));
    REQUIRE(a.valid());
    REQUIRE(b.valid());
    std::vector<VehicleId> trains;
    for (int i = 0; i < 2; ++i) {
        const CommandResult buy = tw.run(CmdBuyVehicle{depot, 7});
        REQUIRE(buy.ok);
        const VehicleId id(buy.createdId);
        REQUIRE(tw.run(CmdAddCarriage{id, 11}).ok);
        Order oa, ob;
        oa.station = a;
        ob.station = b;
        REQUIRE(tw.run(CmdSetOrders{id, {oa, ob}}).ok);
        REQUIRE(tw.run(CmdStartStopVehicle{id}).ok);
        trains.push_back(id);
    }
    i64 lastA = -1, lastB = -1, visitsA = 0, visitsB = 0;
    bool shared = false;
    for (i64 t = 0; t < 2 * kTicksPerDay; ++t) {
        tw.w.tick();
        if (tw.w.stations[a].lastVisitTick != lastA) {
            lastA = tw.w.stations[a].lastVisitTick;
            ++visitsA;
        }
        if (tw.w.stations[b].lastVisitTick != lastB) {
            lastB = tw.w.stations[b].lastVisitTick;
            ++visitsB;
        }
        // Never two trains on one edge.
        const Vehicle& v0 = tw.w.vehicles[trains[0]];
        const Vehicle& v1 = tw.w.vehicles[trains[1]];
        for (const EdgeRun r0 : v0.runs)
            for (const EdgeRun r1 : v1.runs)
                if (r0.edge == r1.edge && v0.state != VehicleState::InDepot && v1.state != VehicleState::InDepot) shared = true;
    }
    CHECK(!shared);
    for (const VehicleId id : trains) {
        CHECK(!tw.w.vehicles[id].lost);
        CHECK(tw.w.vehicles[id].state != VehicleState::InDepot);
    }
    CHECK(visitsA >= 4);
    CHECK(visitsB >= 4);

    const auto bytes = toBytes(tw.w, kSaveVersion);
    World copy;
    fromBytes(bytes, copy, kSaveVersion);
    for (i64 t = 0; t < kTicksPerDay / 2; ++t) {
        tw.w.tick();
        copy.tick();
    }
    CHECK_EQ(copy.stateHash(), tw.w.stateHash());
}

namespace {

// Sets every corner within the rectangle [x0, x1] x [y0, y1] to a fine height.
void setGround(World& w, i32 x0, i32 y0, i32 x1, i32 y1, u16 fine) {
    for (i32 y = y0; y <= y1; ++y)
        for (i32 x = x0; x <= x1; ++x) w.map.setCornerFine(x, y, fine);
}

i32 structureLength(const World& w, TrackStructure kind) {
    i32 total = 0;
    for (const auto [id, e] : w.trackEdges)
        for (const TrackSpan& sp : e.spans)
            if (sp.kind == kind) total += sp.s1 - sp.s0;
    return total;
}

} // namespace

TEST_CASE("track: a line across a valley goes over it on a bridge") {
    TrackWorld tw;
    setGround(tw.w, 14, 1, 22, 2 * kChunkSize - 1, 0);
    const CommandResult r = tw.run(CmdBuildTrack{freeAnchor(at(6, 10.5)), freeAnchor(at(30, 10.5))});
    REQUIRE(r.ok);
    const i32 bridge = structureLength(tw.w, TrackStructure::Bridge);
    CHECK(bridge > 7 * kTrackUnits);
    CHECK(bridge < 11 * kTrackUnits);
    CHECK_EQ(structureLength(tw.w, TrackStructure::Tunnel), 0);
    // The valley floor is untouched and stays clear ground, but nothing may be built under the
    // bridge or reshape the ground beneath it.
    CHECK_EQ(tw.w.map.cornerFine(18, 10), 0);
    CHECK(tw.w.map.tile({18, 10})->type == TileType::Clear);
    CHECK(tw.w.map.tile({8, 10})->type == TileType::Track);
    CHECK(!towns::canPlaceBuilding(tw.w, {18, 10}, 1));
    CHECK(!tw.run(CmdTerraformBrush{{18, 11}, 1, static_cast<u8>(BrushMode::Raise), 0}).ok);

    // Bridges survive a save and load unchanged.
    World copy;
    fromBytes(toBytes(tw.w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(structureLength(copy, TrackStructure::Bridge), bridge);
    CHECK_EQ(copy.stateHash(), tw.w.stateHash());

    // Taking the track away leaves the valley as it was.
    std::vector<TrackEdgeId> edges;
    for (const auto [id, e] : tw.w.trackEdges) edges.push_back(id);
    for (const TrackEdgeId e : edges) REQUIRE(tw.run(CmdRemoveTrack{e}).ok);
    CHECK(tw.w.trackTiles.empty());
    CHECK(tw.w.map.tile({8, 10})->type == TileType::Clear);
}

TEST_CASE("track: a line through a hill goes into a tunnel") {
    TrackWorld tw;
    // A steep hill 128 fine units above the plain around (18, 10).
    for (i32 y = 1; y < 2 * kChunkSize; ++y)
        for (i32 x = 1; x < 2 * kChunkSize; ++x) {
            const double d = std::hypot(x - 18.0, y - 10.5);
            if (d < 7.0) tw.w.map.setCornerFine(x, y, static_cast<u16>(4 * kSubLevels + std::min(128.0, (7.0 - d) * 48.0)));
        }
    const u16 top = tw.w.map.cornerFine(18, 10);
    const CommandResult r = tw.run(CmdBuildTrack{freeAnchor(at(6, 10.5)), freeAnchor(at(30, 10.5))});
    REQUIRE(r.ok);
    const i32 tunnel = structureLength(tw.w, TrackStructure::Tunnel);
    CHECK(tunnel > 3 * kTrackUnits);
    CHECK_EQ(tw.w.map.cornerFine(18, 10), top); // the hill above is left alone
    CHECK(tw.w.map.tile({18, 10})->type == TileType::Clear);
}

TEST_CASE("track: water needs a bridge high enough to cross it") {
    TrackWorld tw;
    setGround(tw.w, 17, 1, 19, 2 * kChunkSize - 1, 0);
    for (i32 y = 0; y < 2 * kChunkSize; ++y)
        for (const i32 x : {17, 18}) tw.w.map.tile({x, y})->type = TileType::Water;
    CHECK(tw.run(CmdBuildTrack{freeAnchor(at(6, 10.5)), freeAnchor(at(30, 10.5))}).ok);
    CHECK(structureLength(tw.w, TrackStructure::Bridge) > 2 * kTrackUnits);

    // Banks barely above the water are too low for a bridge.
    TrackWorld low;
    setGround(low.w, 1, 1, 2 * kChunkSize - 1, 2 * kChunkSize - 1, kSubLevels);
    setGround(low.w, 17, 1, 19, 2 * kChunkSize - 1, 0);
    for (i32 y = 0; y < 2 * kChunkSize; ++y)
        for (const i32 x : {17, 18}) low.w.map.tile({x, y})->type = TileType::Water;
    const CommandResult r = low.run(CmdBuildTrack{freeAnchor(at(6, 10.5)), freeAnchor(at(30, 10.5))});
    CHECK(!r.ok);
    CHECK(r.error.find("water") != std::string::npos);
}

TEST_CASE("track: level crossings with roads close for trains") {
    TrackWorld tw;
    REQUIRE(tw.run(CmdBuildRoad{{18, 4}, {18, 16}, false}).ok);
    const CommandResult r = tw.run(CmdBuildTrack{freeAnchor(at(6, 10.5)), freeAnchor(at(30, 10.5))});
    REQUIRE(r.ok);
    CHECK(track::isCrossing(tw.w, {18, 10}));
    CHECK(!track::isCrossing(tw.w, {18, 9}));
    CHECK(tw.w.map.tile({18, 10})->type == TileType::Road);
    CHECK(!track::crossingClosed(tw.w, {18, 10}));
    // The crossing closes while a train holds the track over it.
    for (auto [id, e] : tw.w.trackEdges)
        if (!e.crossings.empty()) e.reservedBy = VehicleId{1};
    CHECK(track::crossingClosed(tw.w, {18, 10}));
    for (auto [id, e] : tw.w.trackEdges) e.reservedBy = {};
    // The road can't be taken away from under the track or branch off at the crossing.
    CHECK(!tw.run(CmdRemoveRoad{{18, 10}, {18, 10}, false}).ok);
    CHECK(!tw.run(CmdBuildRoad{{18, 10}, {20, 10}, false}).ok);

    // Track running along a road (or crossing it at a shallow angle) is refused.
    REQUIRE(tw.run(CmdBuildRoad{{8, 20}, {28, 20}, false}).ok);
    CHECK(!tw.run(CmdBuildTrack{freeAnchor(at(6, 19.6)), freeAnchor(at(30, 21.4))}).ok);
}

TEST_CASE("track: one line may cross another on a bridge") {
    TrackWorld tw;
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(6, 10.5)), freeAnchor(at(30, 10.5))}).ok);
    // Crossing on the level is refused.
    CHECK(!tw.run(CmdBuildTrack{freeAnchor(at(18.5, 1.5)), freeAnchor(at(18.5, 20))}).ok);
    // From hill to hill, high over the first line.
    for (const double cy : {1.5, 20.0})
        for (i32 y = 1; y < 2 * kChunkSize; ++y)
            for (i32 x = 1; x < 2 * kChunkSize; ++x)
                if (std::hypot(x - 18.5, y - cy) < 3.0) tw.w.map.setCornerFine(x, y, 4 * kSubLevels + 60);
    const CommandResult over = tw.run(CmdBuildTrack{freeAnchor(at(18.5, 1.5)), freeAnchor(at(18.5, 20))});
    if (!over.ok) std::printf("overpass refused: %s\n", over.error.c_str());
    CHECK(over.ok);
    CHECK(structureLength(tw.w, TrackStructure::Bridge) > 6 * kTrackUnits);
}

namespace {

// Station A in the west; a line east to a switch, then station B with two parallel platforms:
// platform 1 straight on, platform 2 on a branch that curves out and back to run alongside.
struct TwoPlatformLine {
    TrackWorld tw;
    StationId a, b;
    DepotId depot;
    std::vector<TrackEdgeId> platform1, platform2;

    TwoPlatformLine() {
        World& w = tw.w;
        REQUIRE(tw.run(CmdBuildTrackStation{freeAnchor(at(8, 16)), at(14, 16)}).ok);
        REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(w, at(14, 16), TDir{kDirOne, 0}), freeAnchor(at(22, 16))}).ok);
        REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(w, at(22, 16), TDir{kDirOne, 0}), freeAnchor(at(30, 16))}).ok);
        REQUIRE(tw.run(CmdBuildTrackStation{nodeAnchor(w, at(30, 16)), at(36, 16)}).ok);
        const double t = 0.2126; // tan 12 degrees: two curves of equal chord angle end up parallel
        const CommandResult branch = tw.run(CmdBuildTrack{nodeAnchor(w, at(22, 16), TDir{kDirOne, 0}), freeAnchor(at(29, 16 + 7 * t))});
        if (!branch.ok) std::printf("branch refused: %s\n", branch.error.c_str());
        REQUIRE(branch.ok);
        REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(w, at(29, 16 + 7 * t), TDir{kDirOne, 0}), freeAnchor(at(36, 16 + 14 * t))}).ok);
        const CommandResult p2 = tw.run(CmdBuildTrackStation{nodeAnchor(w, at(36, 16 + 14 * t)), at(42, 16 + 14 * t)});
        if (!p2.ok) std::printf("platform 2 refused: %s\n", p2.error.c_str());
        REQUIRE(p2.ok);
        const CommandResult dep = tw.run(CmdBuildTrackDepot{track::nodeNear(w, at(8, 16), 40)});
        REQUIRE(dep.ok);
        depot = DepotId(dep.createdId);
        a = stationAt(w, at(11, 16));
        b = stationAt(w, at(33, 16));
        REQUIRE(a.valid());
        REQUIRE(b.valid());
        REQUIRE(track::platformsOf(w, b).size() == 2u);
        platform1 = track::platformChain(w, track::platformNear(w, b, at(33, 16)));
        platform2 = track::platformChain(w, track::platformNear(w, b, at(39, 16 + 14 * t)));
        REQUIRE(!platform1.empty());
        REQUIRE(!platform2.empty());
        CHECK(platform1 != platform2);
    }

    VehicleId train(std::vector<Order> orders) {
        const CommandResult buy = tw.run(CmdBuyVehicle{depot, 7});
        REQUIRE(buy.ok);
        const VehicleId id(buy.createdId);
        REQUIRE(tw.run(CmdAddCarriage{id, 11}).ok);
        const CommandResult set = tw.run(CmdSetOrders{id, orders});
        if (!set.ok) std::printf("orders refused: %s\n", set.error.c_str());
        REQUIRE(set.ok);
        REQUIRE(tw.run(CmdStartStopVehicle{id}).ok);
        return id;
    }

    // Which platform of B the train is loading at: 1, 2 or 0 (elsewhere).
    int loadingAt(VehicleId id) const {
        const Vehicle& v = tw.w.vehicles[id];
        if (v.state != VehicleState::Loading || v.runs.empty()) return 0;
        const TrackEdgeId e = v.runs.front().edge;
        if (std::ranges::find(platform1, e) != platform1.end()) return 1;
        if (std::ranges::find(platform2, e) != platform2.end()) return 2;
        return 0;
    }
};

Order stopAt(StationId s) {
    Order o;
    o.station = s;
    return o;
}

} // namespace

TEST_CASE("track: an order can send a train to one platform in particular") {
    TwoPlatformLine line;
    Order toB = stopAt(line.b);
    toB.flags = kOrderPlatform;
    toB.platform = track::platformPoint(line.tw.w, line.platform2);
    CHECK_EQ(track::platformNumber(line.tw.w, line.b, toB.platform), 2);
    const VehicleId id = line.train({stopAt(line.a), toB});
    int stops1 = 0, stops2 = 0;
    i64 lastB = -1;
    for (i64 t = 0; t < kTicksPerDay; ++t) {
        line.tw.w.tick();
        if (line.tw.w.stations[line.b].lastVisitTick != lastB) {
            lastB = line.tw.w.stations[line.b].lastVisitTick;
            const int at = line.loadingAt(id);
            stops1 += at == 1;
            stops2 += at == 2;
        }
    }
    CHECK(!line.tw.w.vehicles[id].lost);
    CHECK(stops2 >= 2);
    CHECK_EQ(stops1, 0);

    // The chosen platform survives a save and load.
    World copy;
    fromBytes(toBytes(line.tw.w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.vehicles[id].orders == line.tw.w.vehicles[id].orders);

    // A platform that is not part of the station is refused.
    Order bad = toB;
    bad.platform = at(60, 60);
    CHECK(!line.tw.run(CmdSetOrders{id, {stopAt(line.a), bad}}).ok);
}

TEST_CASE("track: with any platform allowed a train takes a free one") {
    TwoPlatformLine line;
    // The first train waits for a full load at platform 1 (nobody comes), keeping it busy.
    Order hold = stopAt(line.b);
    hold.flags = kOrderFullLoad | kOrderPlatform;
    hold.platform = track::platformPoint(line.tw.w, line.platform1);
    const VehicleId first = line.train({stopAt(line.a), hold});
    for (i64 t = 0; t < kTicksPerDay && line.loadingAt(first) != 1; ++t) line.tw.w.tick();
    REQUIRE(line.loadingAt(first) == 1);
    const VehicleId second = line.train({stopAt(line.a), stopAt(line.b)});
    int seen = 0;
    for (i64 t = 0; t < kTicksPerDay && seen == 0; ++t) {
        line.tw.w.tick();
        seen = line.loadingAt(second);
    }
    CHECK_EQ(seen, 2);
}

TEST_CASE("track: trains on free track pass through waypoints") {
    TwoPlatformLine line;
    Order via;
    via.kind = OrderKind::Via;
    via.waypoint = {26, 16};
    const VehicleId id = line.train({stopAt(line.a), via, stopAt(line.b)});
    i64 lastA = -1, lastB = -1, visitsA = 0, visitsB = 0;
    for (i64 t = 0; t < kTicksPerDay; ++t) {
        line.tw.w.tick();
        if (line.tw.w.stations[line.a].lastVisitTick != lastA) {
            lastA = line.tw.w.stations[line.a].lastVisitTick;
            ++visitsA;
        }
        if (line.tw.w.stations[line.b].lastVisitTick != lastB) {
            lastB = line.tw.w.stations[line.b].lastVisitTick;
            ++visitsB;
        }
    }
    CHECK(!line.tw.w.vehicles[id].lost);
    CHECK(visitsA >= 3);
    CHECK(visitsB >= 3);
    // A waypoint needs track under it.
    Order nowhere = via;
    nowhere.waypoint = {26, 40};
    CHECK(!line.tw.run(CmdSetOrders{id, {stopAt(line.a), nowhere}}).ok);
}

TEST_CASE("track: stations are built for passengers or freight, with several platforms at once") {
    TrackWorld tw;
    // A freight station of three 8-tile platforms, side by side two tiles apart.
    const CommandResult built = tw.run(CmdBuildTrackStation{freeAnchor(at(8, 10)), at(16, 10), {}, StationUse::Freight, 3});
    REQUIRE(built.ok);
    const StationId freight(built.createdId);
    REQUIRE(tw.w.stations.contains(freight));
    CHECK(tw.w.stations[freight].use == StationUse::Freight);
    const auto platforms = track::platformsOf(tw.w, freight);
    REQUIRE(platforms.size() == 3u);
    for (const auto& chain : platforms) {
        i32 length = 0;
        for (const TrackEdgeId e : chain) length += tw.w.trackEdges[e].length;
        CHECK(std::abs(length - 8 * kTrackUnits) < 8);
    }
    CHECK(!tw.w.stations[freight].takesPassengers());
    CHECK(tw.w.stations[freight].takesFreight());
    // Freight stations have no passenger entrances; passengers do not walk to them.
    CHECK(!tw.run(CmdStationEntrance{freight, {12, 14}, false}).ok);
    std::vector<StationId> reach;
    passengers::accessStations(tw.w, {2 * 12 + 1, 2 * 13 + 1}, reach);
    CHECK(std::ranges::find(reach, freight) == reach.end());
    // A passenger platform built beside it starts a station of its own rather than joining.
    const CommandResult beside = tw.run(CmdBuildTrackStation{freeAnchor(at(8, 18)), at(14, 18), {}, StationUse::Passengers, 1});
    REQUIRE(beside.ok);
    CHECK(StationId(beside.createdId) != freight);
    CHECK(tw.w.stations[StationId(beside.createdId)].use == StationUse::Passengers);
    // Too many platforms, and a station turned over to both.
    CHECK(!tw.run(CmdBuildTrackStation{freeAnchor(at(30, 10)), at(36, 10), {}, StationUse::Freight, 5}).ok);
    REQUIRE(tw.run(CmdSetStationUse{freight, StationUse::Both}).ok);
    CHECK(tw.w.stations[freight].takesPassengers());
    // The use survives saves; stations saved before it existed take both.
    tw.w.stations[freight].use = StationUse::Freight;
    World copy;
    fromBytes(toBytes(tw.w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.stations[freight].use == StationUse::Freight);
    World older;
    fromBytes(toBytes(tw.w, 30), older, 30);
    CHECK(older.stations[freight].use == StationUse::Both);
}

TEST_CASE("track: a station's platforms run side by side whichever way it points") {
    TrackWorld tw;
    // Pointing south-east: every platform runs the same way, two tiles beside the last.
    const CommandResult built = tw.run(CmdBuildTrackStation{freeAnchor(at(10, 10)), at(16, 16), {}, StationUse::Passengers, 3});
    REQUIRE(built.ok);
    const auto platforms = track::platformsOf(tw.w, StationId(built.createdId));
    REQUIRE(platforms.size() == 3u);
    std::vector<std::pair<TPoint, TPoint>> ends;
    for (const auto& chain : platforms)
        ends.push_back({tw.w.trackEdges[chain.front()].samples.front().p, tw.w.trackEdges[chain.back()].samples.back().p});
    for (usize k = 1; k < ends.size(); ++k) {
        const i64 ax = ends[0].second.x - ends[0].first.x, az = ends[0].second.z - ends[0].first.z;
        const i64 bx = ends[k].second.x - ends[k].first.x, bz = ends[k].second.z - ends[k].first.z;
        CHECK(std::abs(ax * bz - az * bx) < 8 * kTrackUnits * kTrackUnits / 100); // parallel
        const double gap = std::hypot(double(ends[k].first.x - ends[0].first.x), double(ends[k].first.z - ends[0].first.z));
        CHECK(std::abs(gap - 2.0 * kTrackUnits * static_cast<double>(k)) < kTrackUnits / 4.0);
    }
}

namespace {

// The edge running over a point, with the anchor for joining it there.
TrackAnchor alongAnchor(const World& w, TPoint p, TDir into = {0, 0}) {
    TrackAnchor a;
    a.edge = track::edgeNear(w, p, 64);
    a.pos = p;
    a.into = into;
    return a;
}

// Total length of the edges with both ends on the line z = row (tiles).
i64 lengthAlong(const World& w, double row) {
    i64 total = 0;
    for (const auto [id, e] : w.trackEdges) {
        const TrackNode& a = w.trackNodes[e.a];
        const TrackNode& b = w.trackNodes[e.b];
        if (std::abs(a.pos.z - row * kTrackUnits) < 4 && std::abs(b.pos.z - row * kTrackUnits) < 4) total += e.length;
    }
    return total;
}

} // namespace

TEST_CASE("track: new track joins existing track part way along a piece, at either end") {
    TrackWorld tw;
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(5, 12)), freeAnchor(at(25, 12))}).ok);
    REQUIRE(tw.w.trackEdges.size() == 3u);
    const i64 before = lengthAlong(tw.w, 12);

    // Too near a joint to split: use the joint itself.
    const TrackAnchor nearJoint = alongAnchor(tw.w, at(11.8, 12), TDir{kDirOne, 0});
    CHECK(!tw.run(CmdBuildTrack{nearJoint, freeAnchor(at(20, 18))}).ok);
    // Branching must leave in line with the track.
    CHECK(!tw.run(CmdBuildTrack{alongAnchor(tw.w, at(8, 12), TDir{0, kDirOne}), freeAnchor(at(8, 20))}).ok);
    // Nor may it run along on top of the track.
    CHECK(!tw.run(CmdBuildTrack{alongAnchor(tw.w, at(8, 12), TDir{kDirOne, 0}), freeAnchor(at(11, 12))}).ok);

    // Starting part way along the first piece: a switch appears there.
    const TrackAnchor from = alongAnchor(tw.w, at(8, 12), TDir{kDirOne, 0});
    REQUIRE(from.edge.valid());
    REQUIRE(tw.run(CmdBuildTrack{from, freeAnchor(at(17, 19))}).ok);
    const TrackNodeId first = track::nodeNear(tw.w, at(8, 12), 32, true);
    REQUIRE(first.valid());
    CHECK(tw.w.trackNodes[first].count(0) == 2);
    CHECK(tw.w.trackNodes[first].count(1) == 1);
    CHECK(std::abs(tw.w.trackNodes[first].h - tw.w.trackNodes[track::nodeNear(tw.w, at(5, 12), 32, true)].h) < 4);

    // Ending part way along the second piece, arriving from the north heading east.
    const TrackAnchor to = alongAnchor(tw.w, at(15, 12), TDir{-kDirOne, 0});
    REQUIRE(to.edge.valid());
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(4, 5)), to}).ok);
    const TrackNodeId second = track::nodeNear(tw.w, at(15, 12), 32, true);
    REQUIRE(second.valid());
    CHECK(tw.w.trackNodes[second].count(1) == 2);
    CHECK(tw.w.trackNodes[second].count(0) == 1);

    // The main line keeps its shape, in four pieces now.
    CHECK(std::abs(lengthAlong(tw.w, 12) - before) < 16);
    for (const auto [id, e] : tw.w.trackEdges) CHECK(e.minRadius == 0 || e.minRadius >= kTrackMinRadius);
    CHECK(tw.w.map.tile({8, 12})->type == TileType::Track);
    CHECK(tw.w.map.tile({15, 12})->type == TileType::Track);

    // Saves keep the network; removing everything leaves clear land.
    const auto bytes = toBytes(tw.w, kSaveVersion);
    World copy;
    fromBytes(bytes, copy, kSaveVersion);
    CHECK_EQ(copy.stateHash(), tw.w.stateHash());
    std::vector<TrackEdgeId> ids;
    for (const auto [id, e] : tw.w.trackEdges) ids.push_back(id);
    for (const TrackEdgeId id : ids) CHECK(tw.run(CmdRemoveTrack{id}).ok);
    CHECK_EQ(tw.w.trackNodes.size(), 0u);
    CHECK(tw.w.trackTiles.empty());
    for (i32 x = 4; x <= 26; ++x) CHECK(tw.w.map.tile({x, 12})->type == TileType::Clear);
}

TEST_CASE("track: signals go anywhere along track, change, come off again, and divide blocks") {
    TrackWorld tw;
    REQUIRE(tw.run(CmdBuildTrack{freeAnchor(at(5, 12)), freeAnchor(at(13, 12))}).ok);
    REQUIRE(tw.w.trackEdges.size() == 1u);
    const TrackEdgeId edge = track::edgeNear(tw.w, at(9, 12), 32);
    const Money money = tw.w.companies[tw.co].money;
    // Not too near an open end, nor at the end itself.
    CHECK(!tw.run(CmdTrackSignal{{}, edge, at(5.2, 12), 3}).ok);
    CHECK(!tw.run(CmdTrackSignal{track::nodeNear(tw.w, at(5, 12), 32, true)}).ok);

    // A one-way signal part way along: a node is added for it.
    const CommandResult placed = tw.run(CmdTrackSignal{{}, edge, at(9, 12.2), 1});
    REQUIRE(placed.ok);
    CHECK(tw.w.companies[tw.co].money < money);
    const TrackNodeId node(placed.createdId);
    REQUIRE(tw.w.trackNodes.contains(node));
    CHECK(tw.w.trackNodes[node].signals == 1);
    CHECK(distance(tw.w.trackNodes[node].pos, at(9, 12)) < 8);
    CHECK(tw.w.trackEdges.size() == 2u);
    const TrackEdgeId west = tw.w.trackNodes[node].edges[2], east = tw.w.trackNodes[node].edges[0];
    {
        const auto blocks = track::signalBlocks(tw.w);
        CHECK(blocks.at(west.value) != blocks.at(east.value));
    }
    // Two-way, then the same again (refused), then off.
    CHECK(tw.run(CmdTrackSignal{node, {}, {}, 3}).ok);
    CHECK(tw.w.trackNodes[node].signals == 3);
    CHECK(!tw.run(CmdTrackSignal{node, {}, {}, 3}).ok);
    CHECK(tw.run(CmdTrackSignal{node, {}, {}, 0}).ok);
    CHECK(tw.w.trackNodes[node].signals == 0);
    CHECK(!tw.run(CmdTrackSignal{node, {}, {}, 0}).ok);
    const auto blocks = track::signalBlocks(tw.w);
    CHECK(blocks.at(west.value) == blocks.at(east.value));
}

TEST_CASE("track: splitting track a train holds keeps the train on its way") {
    TrackWorld tw;
    REQUIRE(tw.run(CmdBuildTrackStation{freeAnchor(at(8, 16)), at(14, 16)}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(14, 16), TDir{kDirOne, 0}), freeAnchor(at(40, 18))}).ok);
    TrackAnchor bStart;
    bStart.node = track::nodeNear(tw.w, at(40, 18), 40);
    REQUIRE(tw.run(CmdBuildTrackStation{bStart, at(46, 20)}).ok);
    const CommandResult dep = tw.run(CmdBuildTrackDepot{track::nodeNear(tw.w, at(8, 16), 40)});
    REQUIRE(dep.ok);
    const StationId a = stationAt(tw.w, at(11, 16));
    const StationId b = stationAt(tw.w, at(43, 19));
    const CommandResult buy = tw.run(CmdBuyVehicle{DepotId(dep.createdId), 7});
    REQUIRE(buy.ok);
    const VehicleId train(buy.createdId);
    REQUIRE(tw.run(CmdAddCarriage{train, 11}).ok);
    Order oa, ob;
    oa.station = a;
    ob.station = b;
    REQUIRE(tw.run(CmdSetOrders{train, {oa, ob}}).ok);
    REQUIRE(tw.run(CmdStartStopVehicle{train}).ok);

    // Wait until the train is out on the line with its head part way along a piece of it.
    const Vehicle& v = tw.w.vehicles[train];
    const auto headOnLine = [&] {
        if (v.runs.empty() || v.speed == 0) return false;
        const TrackEdge& e = tw.w.trackEdges[v.runs.front().edge];
        return e.kind == TrackKind::Plain && e.length > 3 * kTrackUnits && v.progress > kTrackUnits &&
               e.length - v.progress > 2 * kTrackUnits;
    };
    for (i64 t = 0; t < kTicksPerDay && !headOnLine(); ++t) tw.w.tick();
    REQUIRE(headOnLine());
    const EdgeRun head = v.runs.front();
    const TrackEdge& under = tw.w.trackEdges[head.edge];
    const TPoint headAt = track::pointOn(under, head.forward, v.progress).p;
    // Signals just behind and just ahead of the head, on the piece it is on.
    const TPoint behind = track::pointOn(under, head.forward, v.progress - kTrackUnits / 2).p;
    REQUIRE(tw.run(CmdTrackSignal{{}, head.edge, behind, 3}).ok);
    const TrackEdgeId now = v.runs.front().edge;
    const TrackSample ahead = track::pointOn(tw.w.trackEdges[now], v.runs.front().forward, v.progress + kTrackUnits);
    REQUIRE(tw.run(CmdTrackSignal{{}, now, ahead.p, 3}).ok);
    // The head has not moved, and the train holds every piece it is on or has reserved.
    CHECK(distance(track::pointOn(tw.w.trackEdges[v.runs.front().edge], v.runs.front().forward, v.progress).p, headAt) < 8);
    for (const auto* runs : {&v.runs, &v.runsAhead})
        for (const EdgeRun r : *runs) {
            REQUIRE(tw.w.trackEdges.contains(r.edge));
            CHECK(tw.w.trackEdges[r.edge].reservedBy == train);
        }
    // Consecutive runs meet at a node.
    for (usize i = 0; i + 1 < v.runs.size(); ++i) CHECK(track::runStart(tw.w, v.runs[i]) == track::runEnd(tw.w, v.runs[i + 1]));

    i64 lastA = tw.w.stations[a].lastVisitTick, lastB = tw.w.stations[b].lastVisitTick;
    int visits = 0;
    for (i64 t = 0; t < kTicksPerDay; ++t) {
        tw.w.tick();
        if (tw.w.stations[a].lastVisitTick != lastA) {
            lastA = tw.w.stations[a].lastVisitTick;
            ++visits;
        }
        if (tw.w.stations[b].lastVisitTick != lastB) {
            lastB = tw.w.stations[b].lastVisitTick;
            ++visits;
        }
    }
    CHECK(!v.lost);
    CHECK(visits >= 4);
}
