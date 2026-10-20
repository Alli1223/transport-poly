#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/road.hpp"
#include "sim/walking.hpp"
#include "sim/world.hpp"
#include "test.hpp"
#include <cmath>
#ifdef TP_TEST_RENDERING
#include "render/person_pose.hpp"
#include "render/track_renderer.hpp"
#endif
using namespace tp;
namespace {
struct Streets {
    World w;
    CompanyId co;
    Streets() {
        for (i32 y = 0; y < 2; ++y)
            for (i32 x = 0; x < 2; ++x) w.map.activate({x, y});
        for (i32 y = 1; y < 64; ++y)
            for (i32 x = 1; x < 64; ++x) {
                w.map.setCornerHeight(x, y, 4);
                *w.map.tile({x, y}) = Tile{};
            }
        Command c;
        c.payload = CmdCreateCompany{"Walk test", 0xFF};
        co = CompanyId(static_cast<u8>(executeCommand(w, c).createdId));
        w.companies[co].money = 50'000'000;
    }
    Command command(CommandPayload p) {
        Command c;
        c.company = co;
        c.payload = std::move(p);
        return c;
    }
    CommandResult run(CommandPayload p) { return executeCommand(w, command(std::move(p))); }
    StationId platform() {
        TrackAnchor a;
        a.pos = tileCentre({10, 20});
        const auto result = run(CmdBuildTrackStation{a, tileCentre({18, 20})});
        REQUIRE(result.ok);
        return StationId(result.createdId);
    }
};
} // namespace
TEST_CASE("walking: pedestrians take connected pavements around obstacles instead of a straight line") {
    Streets f;
    REQUIRE(f.run(CmdBuildRoad{{8, 8}, {8, 16}}).ok);
    REQUIRE(f.run(CmdBuildRoad{{8, 16}, {14, 16}}).ok);
    REQUIRE(f.run(CmdBuildRoad{{14, 16}, {14, 8}}).ok);
    const auto path = walking::route(f.w, {8 * 256 + 40, 8 * 256 + 40}, {14 * 256 + 40, 8 * 256 + 40});
    REQUIRE(path.has_value());
    CHECK(walking::length(*path) > 18 * kTrackUnits);
    bool turned = false;
    for (const auto p : *path) {
        const auto at = tileAt(p.p);
        const auto* tile = f.w.map.tile(at);
        REQUIRE(tile);
        CHECK(tile->type == TileType::Road);
        const i32 x = p.p.x - at.x * kTrackUnits, z = p.p.z - at.y * kTrackUnits;
        CHECK(x == 40 || x == 216);
        CHECK(z == 40 || z == 216);
        turned = turned || at.y == 16;
    }
    CHECK(turned);
    f.w.map.tile({8, 12})->type = TileType::Water;
    CHECK(!walking::route(f.w, path->front().p, path->back().p));
}
TEST_CASE("walking: building doors face their road and disconnected trips use abstract travel") {
    Streets f;
    const auto id = f.w.buildings.create();
    auto& b = f.w.buildings[id];
    b.pos = {10, 10};
    b.facing = Dir::S;
    auto& tile = *f.w.map.tile(b.pos);
    tile.type = TileType::Building;
    tile.index = id.value;
    CHECK(walking::endpoint(f.w, b.centre2()) == (TPoint{10 * 256 + 128, 11 * 256 + 1}));
    const auto person = f.w.people.create();
    people::travelAbstract(f.w, person, {21, 21}, {29, 21}, true);
    CHECK(f.w.people[person].state == PersonState::Lift);
    CHECK(f.w.people[person].walkPath.empty());
    REQUIRE(f.run(CmdBuildRoad{{9, 11}, {16, 11}}).ok);
    people::travelAbstract(f.w, person, b.centre2(), {29, 23}, true);
    CHECK(f.w.people[person].state == PersonState::Walking);
    CHECK(!f.w.people[person].walkPath.empty());
}
TEST_CASE("walking: changing road connections invalidates a saved pavement route") {
    Streets f;
    REQUIRE(f.run(CmdBuildRoad{{8, 10}, {18, 10}}).ok);
    const auto path = walking::route(f.w, {8 * 256 + 40, 10 * 256 + 40}, {14 * 256 + 40, 10 * 256 + 40});
    REQUIRE(path);
    const auto id = f.w.people.create();
    auto& p = f.w.people[id];
    p.state = PersonState::Walking;
    p.tripFrom2 = {17, 21};
    p.tripTo2 = {29, 21};
    REQUIRE(walking::begin(f.w, id, *path, 0));
    f.w.map.tile({11, 10})->road = axisBits(Dir::N);
    walking::invalidate(f.w);
    CHECK(p.state == PersonState::Lift);
    CHECK(p.walkPath.empty());
}
TEST_CASE("walking: old saves migrate active walkers to road paths") {
    Streets f;
    REQUIRE(f.run(CmdBuildRoad{{8, 10}, {18, 10}}).ok);
    const auto id = f.w.people.create();
    auto& p = f.w.people[id];
    p.state = PersonState::Walking;
    p.tripFrom2 = {17, 21};
    p.tripTo2 = {29, 21};
    people::schedule(f.w, id, 180);
    World loaded;
    fromBytes(toBytes(f.w, 17), loaded, 17);
    CHECK(loaded.people[id].state == PersonState::Walking);
    CHECK(!loaded.people[id].walkPath.empty());
    CHECK(loaded.people[id].walkEnd == loaded.people[id].nextEvent);
}
TEST_CASE("stations: a road entrance creates a protected paved route and survives saves") {
    Streets f;
    const auto sid = f.platform();
    REQUIRE(f.run(CmdBuildRoad{{6, 26}, {30, 26}}).ok);
    CHECK(!walking::toStation(f.w, tileCentre({8, 26}), sid));
    auto c = f.command(CmdStationEntrance{sid, {12, 26}});
    const auto before = f.w.stateHash();
    const auto preview = testCommand(f.w, c);
    REQUIRE(preview.ok);
    CHECK_EQ(before, f.w.stateHash());
    REQUIRE(executeCommand(f.w, c).ok);
    const auto& s = f.w.stations[sid];
    REQUIRE(!s.entrances.empty());
    for (const auto tile : s.entrances.front().tiles) {
        CHECK(f.w.map.tile(tile)->type == TileType::Footpath);
        CHECK_EQ(f.w.map.tile(tile)->index, sid.value);
    }
    const auto path = walking::toStation(f.w, {8 * 256 + 40, 26 * 256 + 40}, sid);
    REQUIRE(path);
    CHECK(path->back().p == s.entrances.front().path.back());
    const auto exit = walking::fromStation(f.w, sid, path->front().p);
    REQUIRE(exit);
    CHECK(exit->back().p == path->front().p);
    World loaded;
    fromBytes(toBytes(f.w, kSaveVersion), loaded, kSaveVersion);
    CHECK_EQ(loaded.stateHash(), f.w.stateHash());
    CHECK(walking::toStation(loaded, path->front().p, sid) == path);
    REQUIRE(f.run(CmdStationEntrance{sid, {}, true}).ok);
    CHECK(f.w.stations[sid].entrances.empty());
    CHECK(!walking::toStation(f.w, path->front().p, sid));
}
TEST_CASE("stations: add platforms and extend both ends while keeping station identity and orders") {
    Streets f;
    const auto sid = f.platform();
    Vehicle train;
    train.model = 7;
    train.cars = {7, 11};
    train.owner = f.co;
    Order order;
    order.station = sid;
    train.orders = {order};
    const auto vehicle = f.w.vehicles.create(train);
    const auto waiting = f.w.people.create();
    f.w.people[waiting].state = PersonState::Waiting;
    f.w.people[waiting].station = sid;
    f.w.stations[sid].waiting[sid].push_back(waiting);
    const auto first = track::platformsOf(f.w, sid).front().front();
    const auto before = f.w.stateHash();
    auto cmd = f.command(CmdAddPlatform{sid, first, 1, 8});
    REQUIRE(testCommand(f.w, cmd).ok);
    CHECK_EQ(before, f.w.stateHash());
    REQUIRE(executeCommand(f.w, cmd).ok);
    CHECK_EQ(f.w.stations.size(), 1u);
    CHECK_EQ(track::platformsOf(f.w, sid).size(), 2u);
    auto ends = track::platformEnds(f.w, first);
    REQUIRE(ends.size() == 2);
    REQUIRE(f.run(CmdExtendPlatform{sid, ends[0].node, 4}).ok);
    REQUIRE(f.run(CmdExtendPlatform{sid, ends[1].node, 4}).ok);
    i32 length = 0;
    for (const auto e : track::platformChain(f.w, first)) length += f.w.trackEdges[e].length;
    CHECK(length >= 15 * kTrackUnits);
    CHECK_EQ(track::platformsOf(f.w, sid).size(), 2u);
    CHECK(f.w.vehicles[vehicle].orders == train.orders);
    CHECK_EQ(f.w.stations[sid].waitingCount(), 1u);
    World loaded;
    fromBytes(toBytes(f.w, kSaveVersion), loaded, kSaveVersion);
    CHECK_EQ(loaded.stateHash(), f.w.stateHash());
    auto bad = f.command(CmdAddPlatform{sid, first, 1, 8});
    bad.company = {};
    CHECK(!executeCommand(f.w, bad).ok);
    f.w.trackEdges[first].reservedBy = VehicleId{0};
    CHECK(!f.run(CmdExtendPlatform{sid, track::platformEnds(f.w, first)[0].node, 4}).ok);
}

TEST_CASE("walking: saved motion follows the same street corners and arrives on schedule") {
    Streets f;
    REQUIRE(f.run(CmdBuildRoad{{8, 10}, {18, 10}}).ok);
    const auto home = f.w.buildings.create(), work = f.w.buildings.create();
    f.w.buildings[home].pos = {9, 9};
    f.w.buildings[home].serial = 1;
    f.w.buildings[work].pos = {14, 9};
    f.w.buildings[work].serial = 2;
    for (auto id : {home, work}) {
        auto* tile = f.w.map.tile(f.w.buildings[id].pos);
        tile->type = TileType::Building;
        tile->index = id.value;
    }
    Person person;
    person.serial = 3;
    person.home = f.w.buildings[home].ref(home);
    person.work = f.w.buildings[work].ref(work);
    person.purpose = TripPurpose::Work;
    const auto id = f.w.people.create(person);
    people::travelAbstract(f.w, id, f.w.buildings[home].centre2(), f.w.buildings[work].centre2(), true);
    REQUIRE(f.w.people[id].state == PersonState::Walking);
    World saved;
    fromBytes(toBytes(f.w, kSaveVersion), saved, kSaveVersion);
    const auto until = f.w.people[id].walkEnd;
    for (i64 tick = 0; tick <= until; ++tick) {
        CHECK(walking::position(f.w.people[id], f.w.clock.tick) ==
              walking::position(saved.people[id], saved.clock.tick));
#ifdef TP_TEST_RENDERING
        if (f.w.people[id].state == PersonState::Walking) {
            const auto pose = gfx::personPose(f.w, f.w.people[id], 0.5);
            REQUIRE(pose);
            CHECK(pose->visible);
        }
#endif
        f.w.tick();
        saved.tick();
    }
    CHECK(f.w.people[id].state == PersonState::AtWork);
    CHECK_EQ(f.w.stateHash(), saved.stateHash());
}

TEST_CASE("stations: passengers cross between platforms using a raised concourse and stairs") {
    Streets f;
    const auto sid = f.platform();
    const auto first = track::platformsOf(f.w, sid).front().front();
    REQUIRE(f.run(CmdAddPlatform{sid, first, 1, 8}).ok);
    const auto platforms = track::platformsOf(f.w, sid);
    const auto& a = f.w.trackEdges[platforms[0].front()];
    const auto& b = f.w.trackEdges[platforms[1].front()];
    auto from = track::pointOn(a, true, a.length / 2).p, to = track::pointOn(b, true, b.length / 2).p;
    from.z += 200;
    to.z += 200;
    const auto walk = walking::withinStation(f.w, sid, from, to);
    CHECK(walk.size() > 4u);
    bool elevated = false;
    for (const auto p : walk) elevated = elevated || p.height > f.w.trackNodes[a.a].h;
    CHECK(elevated);
    CHECK(walk.front().p == from);
    CHECK(walk.back().p == to);
    const auto door = track::pointOn(a,true,a.length-64).p;
    const auto along = walking::withinStation(f.w,sid,from,door);
    REQUIRE(along.size() >= 2u);
    for (const auto p : along) {
        CHECK_EQ(p.height, f.w.trackNodes[a.a].h);
        CHECK(p.p.z >= door.z);
    }
    CHECK(along.back().p == door);
    auto opposite = from;
    opposite.z -= 400;
    const auto across = walking::withinStation(f.w, sid, from, opposite);
    CHECK(std::ranges::any_of(across, [&](const WalkPoint& p) { return p.height > f.w.trackNodes[a.a].h; }));
}

TEST_CASE("stations: complete compact footbridges exist without road entrances and follow upgrades") {
    Streets f;
    const auto sid = f.platform();
    const auto first = track::platformsOf(f.w, sid).front().front();
    REQUIRE(f.w.stations[sid].entrances.empty());
    const auto check = [&](usize platforms) {
        const auto stairs = walking::stationConcourse(f.w, sid);
        REQUIRE(stairs.size() == platforms * 2);
        const i32 rail = f.w.trackNodes[f.w.trackEdges[first].a].h;
        for (const auto& stair : stairs) {
            CHECK_EQ(stair.landing.height, rail + 24);
            const auto platform = track::platformsOf(f.w, sid)[stair.platform - 1].front();
            CHECK_EQ(stair.foot.height, f.w.trackNodes[f.w.trackEdges[platform].a].h);
            CHECK(distance(stair.foot.p, stair.landing.p) >= kTrackUnits);
            const auto walk = walking::withinStation(f.w, sid, stair.foot.p, stairs.back().foot.p);
            for (const auto& point : walk) CHECK(point.height <= rail + 24);
        }
#ifdef TP_TEST_RENDERING
        gfx::MeshBuilder mesh;
        gfx::buildStationConcourseMesh(f.w, sid, {}, mesh);
        REQUIRE(!mesh.vertices.empty());
        for (const auto& vertex : mesh.vertices) {
            CHECK(std::isfinite(vertex.pos.x));
            CHECK(vertex.pos.y < (rail + 24) / 32.0f + 0.3f);
        }
#endif
    };
    check(1);
    REQUIRE(f.run(CmdAddPlatform{sid, first, 1, 8}).ok);
    REQUIRE(f.run(CmdAddPlatform{sid, first, -1, 8}).ok);
    check(3);
    const auto end = track::platformEnds(f.w, first).front().node;
    REQUIRE(f.run(CmdExtendPlatform{sid, end, 4}).ok);
    check(3);
}

TEST_CASE("stations: extending a connected platform keeps real trains shuttling") {
    Streets f;
    const auto a = f.platform();
    const auto end = track::platformEnds(f.w, track::platformsOf(f.w, a).front().front()).back();
    TrackAnchor start;
    start.node = end.node;
    start.pos = f.w.trackNodes[end.node].pos;
    TrackAnchor finish;
    finish.pos = offset(start.pos, end.outward, 16 * kTrackUnits);
    REQUIRE(f.run(CmdBuildTrack{start, finish}).ok);
    TrackAnchor next;
    next.node = track::nodeNear(f.w, finish.pos, 16);
    next.pos = finish.pos;
    const auto built = f.run(CmdBuildTrackStation{next, offset(finish.pos, end.outward, 8 * kTrackUnits)});
    REQUIRE(built.ok);
    const auto b = StationId(built.createdId);
    const auto front = track::platformEnds(f.w, track::platformsOf(f.w, a).front().front()).front();
    const auto depot = f.run(CmdBuildTrackDepot{front.node});
    REQUIRE(depot.ok);
    const auto bought = f.run(CmdBuyVehicle{DepotId(depot.createdId), 7});
    REQUIRE(bought.ok);
    const auto train = VehicleId(bought.createdId);
    REQUIRE(f.run(CmdAddCarriage{train, 11}).ok);
    Order oa;
    oa.station = a;
    Order ob;
    ob.station = b;
    REQUIRE(f.run(CmdSetOrders{train, {oa, ob}}).ok);
    REQUIRE(f.run(CmdExtendPlatform{a, end.node, 4}).ok);
    REQUIRE(f.run(CmdStartStopVehicle{train}).ok);
    for (i64 tick = 0; tick < kTicksPerDay; ++tick) f.w.tick();
    CHECK(f.w.stations[a].lastVisitTick > 0);
    CHECK(f.w.stations[b].lastVisitTick > 0);
    CHECK(!f.w.vehicles[train].lost);
    World saved;
    fromBytes(toBytes(f.w, kSaveVersion), saved, kSaveVersion);
    for (int tick = 0; tick < 300; ++tick) {
        f.w.tick();
        saved.tick();
    }
    CHECK_EQ(f.w.stateHash(), saved.stateHash());
}
TEST_CASE("stations: connected straight track converts into platform without breaking its connections") {
    Streets f;
    const auto sid = f.platform();
    const auto first = track::platformsOf(f.w, sid).front().front();
    const auto ends = track::platformEnds(f.w, first);
    const auto end = ends.back();
    TrackAnchor from;
    from.node = end.node;
    from.pos = f.w.trackNodes[end.node].pos;
    TrackAnchor to;
    to.pos = offset(from.pos, end.outward, 8 * kTrackUnits);
    REQUIRE(f.run(CmdBuildTrack{from, to}).ok);
    const auto edges = f.w.trackEdges.size(), nodes = f.w.trackNodes.size();
    REQUIRE(f.run(CmdExtendPlatform{sid, end.node, 4}).ok);
    CHECK_EQ(f.w.trackEdges.size(), edges);
    CHECK_EQ(f.w.trackNodes.size(), nodes);
    i32 length = 0;
    for (const auto e : track::platformChain(f.w, first)) length += f.w.trackEdges[e].length;
    CHECK(length >= 15 * kTrackUnits);
}
