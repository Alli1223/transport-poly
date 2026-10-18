#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/track.hpp"
#include "sim/world.hpp"

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
    // A ridge across the route and a dip after it.
    for (i32 y = 1; y < 63; ++y)
        for (i32 x = 1; x < 63; ++x) {
            if (x >= 20 && x <= 26) tw.w.map.setCornerFine(x, y, 9 * kSubLevels);
            if (x >= 34 && x <= 40) tw.w.map.setCornerFine(x, y, 1 * kSubLevels);
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
    CHECK(tw.w.map.cornerFine(23, 32) < 9 * kSubLevels);
    CHECK(tw.w.map.cornerFine(37, 32) > 1 * kSubLevels);
    CHECK_EQ(tw.w.map.cornerFine(23, 50), 9 * kSubLevels);
    CHECK_EQ(tw.w.map.cornerFine(37, 10), 1 * kSubLevels);
    // The slope eases out: the cutting's sides are not vertical.
    CHECK(tw.w.map.cornerFine(23, 34) > tw.w.map.cornerFine(23, 32));
    CHECK(tw.w.map.cornerFine(23, 34) < 9 * kSubLevels);
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
