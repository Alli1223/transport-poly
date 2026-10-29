#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/industries.hpp"
#include "sim/prices.hpp"
#include "sim/road.hpp"
#include "sim/road_curves.hpp"
#include "sim/road_path.hpp"
#include "sim/world.hpp"

#if TP_TEST_RENDERING
#include "render/vehicle_pose.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>

using namespace tp;

namespace {

Command cmd(CompanyId company, CommandPayload payload) {
    Command c;
    c.company = company;
    c.payload = std::move(payload);
    return c;
}

// A company on a level, open 40 x 40 tile site at `site` (no towns or industries in the world).
struct CurveWorld {
    World w;
    CompanyId company;
    TilePos site{8, 8};

    explicit CurveWorld(u64 seed = 4242) : w(settings(seed)) {
        for (i32 y = -1; y <= 2; ++y)
            for (i32 x = -1; x <= 2; ++x) w.activateChunk({x, y});
        for (i32 cy = site.y; cy <= site.y + 40; ++cy)
            for (i32 cx = site.x; cx <= site.x + 40; ++cx) w.map.setCornerFine(cx, cy, 4 * kSubLevels);
        for (i32 y = site.y; y < site.y + 40; ++y)
            for (i32 x = site.x; x < site.x + 40; ++x) {
                Tile& t = *w.map.tile({x, y});
                t = Tile{};
                w.map.touch({x, y});
            }
        const CommandResult cr = executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Curves Ltd", 0xFF}));
        company = CompanyId(static_cast<u8>(cr.createdId));
        w.companies[company].money = 10'000'000;
    }
    static WorldSettings settings(u64 seed) {
        WorldSettings s;
        s.seed = seed;
        s.industries = false;
        return s;
    }
    TilePos at(i32 x, i32 y) const { return {site.x + x, site.y + y}; }
    CommandResult run(CommandPayload p) { return executeCommand(w, cmd(company, std::move(p))); }
    CommandResult test(CommandPayload p) { return testCommand(w, cmd(company, std::move(p))); }
};

// No town is founded on the test site: the chunks hold none there (checked).
bool siteIsOpen(const CurveWorld& cw) {
    for (i32 y = -1; y <= 40; ++y)
        for (i32 x = -1; x <= 40; ++x) {
            const Tile* t = cw.w.map.tile(cw.at(x, y));
            if (!t || (x >= 0 && y >= 0 && x < 40 && y < 40 && t->type != TileType::Clear)) return false;
        }
    return true;
}

} // namespace

TEST_CASE("road curves: a quarter turn is laid as a chain of road tiles, priced by its length") {
    CurveWorld cw;
    REQUIRE(siteIsOpen(cw));
    REQUIRE(cw.run(CmdBuildRoad{cw.at(2, 4), cw.at(6, 4), false}).ok);
    const CmdBuildRoadCurve bend{cw.at(6, 4), Dir::E, cw.at(10, 8), Dir::N};
    const u64 before = cw.w.stateHash();
    const CommandResult quote = cw.test(bend);
    REQUIRE(quote.ok);
    CHECK_EQ(cw.w.stateHash(), before); // a quote changes nothing
    const CommandResult built = cw.run(bend);
    REQUIRE(built.ok);
    CHECK_EQ(built.cost, quote.cost);
    REQUIRE(cw.w.roadCurves.size() == 1u);
    const RoadCurve& c = cw.w.roadCurves[RoadCurveId(built.createdId)];
    // A quarter circle of 3.5 tiles.
    const double quarter = 3.14159265 / 2 * 3.5 * kTrackUnits;
    CHECK(std::abs(c.length - quarter) < quarter * 0.03);
    CHECK(c.minRadius > 3 * kTrackUnits && c.minRadius < 4 * kTrackUnits);
    const Money tiles = (c.length + kTrackUnits - 1) / kTrackUnits;
    CHECK_EQ(built.cost, cw.w.inflated(tiles * prices::kRoadCurvePerTile + 2 * prices::kRoadPiece));
    // Every tile between the ends is a road with exactly its two pieces along the curve, owned by the company.
    CHECK(c.tiles.size() >= 6);
    i32 lengths = 0;
    for (usize i = 0; i < c.tiles.size(); ++i) {
        const Tile* t = cw.w.map.tile(c.tiles[i]);
        REQUIRE(t != nullptr);
        CHECK(t->type == TileType::Road);
        CHECK_EQ(roadPieces(*t), static_cast<u8>(dirBit(c.entrySide(i)) | dirBit(c.exitSide(i))));
        CHECK_EQ(t->roadOwner, cw.company.value);
        CHECK(roadcurves::at(cw.w, c.tiles[i]) == RoadCurveId(built.createdId));
        lengths += roadcurves::pathLength(cw.w, c.tiles[i], c.entrySide(i), c.exitSide(i));
        // Driven the other way, each tile is just as long.
        CHECK_EQ(roadcurves::pathLength(cw.w, c.tiles[i], c.exitSide(i), c.entrySide(i)),
                 roadcurves::pathLength(cw.w, c.tiles[i], c.entrySide(i), c.exitSide(i)));
    }
    CHECK(std::abs(lengths - c.length) <= static_cast<i32>(c.tiles.size()));
    CHECK(roadPieces(*cw.w.map.tile(cw.at(6, 4))) == (dirBit(Dir::W) | dirBit(Dir::E)));
    CHECK(roadPieces(*cw.w.map.tile(cw.at(10, 8))) == dirBit(Dir::N));
    // Road vehicles can drive from one end to the other, along the curve.
    const auto path = findRoadPath(cw.w, cw.at(2, 4), Dir::W, {cw.at(10, 8)}, VehicleType::Road, {});
    REQUIRE(path.has_value());
    CHECK_EQ(path->size(), c.tiles.size() + 5);
    // The tiles it overhangs keep clear of buildings.
    CHECK(!c.verge.empty());
    for (const TilePos v : c.verge) {
        CHECK(roadcurves::onVerge(cw.w, v));
        CHECK(!towns::canPlaceBuilding(cw.w, v, 1));
    }
}

TEST_CASE("road curves: S-curves between parallel roads, and curves that can't be built") {
    CurveWorld cw;
    REQUIRE(siteIsOpen(cw));
    // An S to one side between two straight roads.
    REQUIRE(cw.run(CmdBuildRoad{cw.at(2, 20), cw.at(5, 20), false}).ok);
    REQUIRE(cw.run(CmdBuildRoad{cw.at(13, 22), cw.at(17, 22), false}).ok);
    const CommandResult s = cw.run(CmdBuildRoadCurve{cw.at(5, 20), Dir::E, cw.at(13, 22), Dir::W});
    REQUIRE(s.ok);
    CHECK(findRoadPath(cw.w, cw.at(2, 20), Dir::W, {cw.at(17, 22)}, VehicleType::Road, {}).has_value());
    const RoadCurveId sId(s.createdId);
    const RoadCurve sCurve = cw.w.roadCurves[sId];

    const auto refused = [&](const CmdBuildRoadCurve& c, std::string_view why) {
        const u64 hash = cw.w.stateHash();
        const CommandResult r = cw.run(c);
        CHECK(!r.ok);
        if (r.error.find(why) == std::string::npos) std::printf("  unexpected refusal: %s\n", r.error.c_str());
        CHECK(r.error.find(why) != std::string::npos);
        CHECK_EQ(cw.w.stateHash(), hash);
    };
    // Back on itself, behind its start, too tight and straight.
    refused({cw.at(4, 4), Dir::E, cw.at(8, 4), Dir::E}, "quarter turn");
    refused({cw.at(8, 4), Dir::E, cw.at(4, 8), Dir::N}, "can't curve round");
    refused({cw.at(4, 4), Dir::E, cw.at(6, 6), Dir::N}, "Too tight");
    refused({cw.at(4, 4), Dir::E, cw.at(12, 4), Dir::W}, "straight");
    // Water, a road and the curve already there in the way.
    const RoadCurvePlan wet = roadcurves::plan(cw.w, cw.at(4, 6), Dir::E, cw.at(8, 10), Dir::N, cw.company.value);
    REQUIRE(wet.ok);
    const TilePos pond = wet.curve.tiles[wet.curve.tiles.size() / 2];
    cw.w.map.tile(pond)->type = TileType::Water;
    refused({cw.at(4, 6), Dir::E, cw.at(8, 10), Dir::N}, "water");
    cw.w.map.tile(pond)->type = TileType::Clear;
    REQUIRE(cw.run(CmdBuildRoad{cw.at(7, 2), cw.at(7, 12), false}).ok);
    refused({cw.at(4, 6), Dir::E, cw.at(9, 11), Dir::N}, "Road in the way");
    refused({cw.at(3, 18), Dir::E, cw.at(8, 23), Dir::N}, "");
    // Unexplored land.
    refused({TilePos{5000, 5000}, Dir::E, TilePos{5004, 5004}, Dir::N}, "not explored");
    // Not enough money: nothing is built.
    cw.w.companies[cw.company].money = 0;
    refused({cw.at(20, 4), Dir::E, cw.at(24, 8), Dir::N}, "Not enough money");
    cw.w.companies[cw.company].money = 10'000'000;

    // Other roads, stops and tram track join a curve at its ends only.
    const TilePos middle = sCurve.tiles[sCurve.tiles.size() / 2];
    for (const Dir d : kDirs) {
        if (roadPieces(*cw.w.map.tile(middle)) & dirBit(d)) continue;
        const CommandResult r = cw.run(CmdBuildRoad{middle, neighbour(neighbour(middle, d), d), false});
        CHECK(!r.ok);
        CHECK(r.error.find("ends only") != std::string::npos || r.error.find("curved road") != std::string::npos);
    }
    const usize mid = sCurve.tiles.size() / 2;
    CHECK(!cw.test(CmdBuildRoad{middle, neighbour(middle, sCurve.exitSide(mid)), true}).ok);
    for (const TilePos t : sCurve.tiles) CHECK(!cw.test(CmdBuildRoadStop{t, false}).ok);

    // Removing any of its tiles takes the whole curve, and the end tiles lose their pieces into it.
    const CommandResult quote = cw.test(CmdRemoveRoad{middle, middle, false});
    REQUIRE(quote.ok);
    CHECK_EQ(quote.cost, cw.w.inflated(roadcurves::removalCost(sCurve)));
    const CommandResult removed = cw.run(CmdRemoveRoad{middle, middle, false});
    REQUIRE(removed.ok);
    CHECK_EQ(removed.cost, quote.cost);
    CHECK(!cw.w.roadCurves.contains(sId));
    for (const TilePos t : sCurve.tiles) {
        CHECK(cw.w.map.tile(t)->type == TileType::Clear);
        CHECK(!roadcurves::at(cw.w, t).valid());
    }
    for (const TilePos v : sCurve.verge) CHECK(!roadcurves::onVerge(cw.w, v));
    CHECK_EQ(roadPieces(*cw.w.map.tile(cw.at(5, 20))), dirBit(Dir::W));
    CHECK_EQ(roadPieces(*cw.w.map.tile(cw.at(13, 22))), dirBit(Dir::E));

    // Bulldozing an area over a curve and its ends: the quote matches what it costs.
    REQUIRE(cw.run(CmdBuildRoadCurve{cw.at(5, 20), Dir::E, cw.at(13, 22), Dir::W}).ok);
    const CommandResult clearQuote = cw.test(CmdClearArea{cw.at(4, 18), cw.at(14, 24)});
    const CommandResult cleared = cw.run(CmdClearArea{cw.at(4, 18), cw.at(14, 24)});
    REQUIRE(cleared.ok);
    CHECK_EQ(cleared.cost, clearQuote.cost);
    CHECK(cw.w.roadCurves.empty());

    // A town's curved road is the town's to keep.
    RoadCurvePlan townCurve = roadcurves::plan(cw.w, cw.at(20, 30), Dir::E, cw.at(24, 34), Dir::N, kOwnerTown);
    REQUIRE(townCurve.ok);
    const RoadCurveId townId = roadcurves::build(cw.w, townCurve);
    const CommandResult r = cw.run(CmdRemoveRoad{cw.w.roadCurves[townId].tiles[1], cw.w.roadCurves[townId].tiles[1], false});
    CHECK(!r.ok);
    CHECK(cw.w.roadCurves.contains(townId));
}

TEST_CASE("road curves: replaying the commands gives the same state, which survives saving") {
    const std::vector<CommandPayload> script = {
        CmdBuildRoad{{10, 12}, {14, 12}, false},
        CmdBuildRoadCurve{{14, 12}, Dir::E, {18, 16}, Dir::N},
        CmdBuildRoad{{18, 16}, {18, 22}, false},
        CmdBuildRoadCurve{{18, 22}, Dir::S, {21, 30}, Dir::N},
        CmdBuildRoadCurve{{10, 30}, Dir::S, {14, 34}, Dir::W},
        CmdRemoveRoad{{11, 31}, {11, 33}, false},
        CmdBuildRoadCurve{{10, 30}, Dir::S, {15, 35}, Dir::W},
    };
    CurveWorld a, b;
    REQUIRE(siteIsOpen(a));
    for (const CommandPayload& p : script) {
        // Commands travel as bytes, as they do between peers.
        const Command sent = cmd(a.company, p);
        Command received;
        fromBytes(toBytes(sent), received);
        const CommandResult ra = executeCommand(a.w, sent);
        const CommandResult rb = executeCommand(b.w, received);
        CHECK_EQ(ra.ok, rb.ok);
        CHECK_EQ(ra.cost, rb.cost);
    }
    CHECK_EQ(a.w.roadCurves.size(), 3u);
    CHECK_EQ(a.w.stateHash(), b.w.stateHash());
    for (int i = 0; i < 200; ++i) {
        a.w.tick();
        b.w.tick();
    }
    CHECK_EQ(a.w.stateHash(), b.w.stateHash());

    // Saved and loaded: the same state, with the curves' shapes and tile index rebuilt.
    World loaded;
    fromBytes(toBytes(a.w, kSaveVersion), loaded, kSaveVersion);
    CHECK_EQ(loaded.stateHash(), a.w.stateHash());
    for (const auto [id, c] : a.w.roadCurves) {
        const RoadCurve* l = loaded.roadCurves.tryGet(id);
        REQUIRE(l != nullptr);
        CHECK_EQ(l->length, c.length);
        CHECK(l->tileStart == c.tileStart);
        CHECK(l->verge == c.verge);
        for (const TilePos t : c.tiles) CHECK(roadcurves::at(loaded, t) == id);
    }
    for (int i = 0; i < 100; ++i) {
        a.w.tick();
        loaded.tick();
    }
    CHECK_EQ(loaded.stateHash(), a.w.stateHash());
}

TEST_CASE("road curves: buses follow a curve at speed, all the way round") {
    CurveWorld cw;
    REQUIRE(siteIsOpen(cw));
    // A stop, a straight, a quarter turn, a straight, an S-curve, a straight and another stop.
    REQUIRE(cw.run(CmdBuildRoad{cw.at(0, 4), cw.at(8, 4), false}).ok);
    const CommandResult bend = cw.run(CmdBuildRoadCurve{cw.at(8, 4), Dir::E, cw.at(13, 9), Dir::N});
    REQUIRE(bend.ok);
    REQUIRE(cw.run(CmdBuildRoad{cw.at(13, 9), cw.at(13, 14), false}).ok);
    const CommandResult s = cw.run(CmdBuildRoadCurve{cw.at(13, 14), Dir::S, cw.at(15, 22), Dir::N});
    REQUIRE(s.ok);
    REQUIRE(cw.run(CmdBuildRoad{cw.at(15, 22), cw.at(15, 30), false}).ok);
    REQUIRE(cw.run(CmdBuildRoadStop{cw.at(2, 4), false}).ok);
    REQUIRE(cw.run(CmdBuildRoadStop{cw.at(15, 28), false}).ok);
    const StationId a(cw.w.map.tile(cw.at(2, 4))->index), b(cw.w.map.tile(cw.at(15, 28))->index);
    const CommandResult depot = cw.run(CmdBuildRoadDepot{cw.at(1, 5), Dir::N, false});
    REQUIRE(depot.ok);
    const CommandResult bought = cw.run(CmdBuyVehicle{DepotId(depot.createdId), 0});
    REQUIRE(bought.ok);
    const VehicleId bus(bought.createdId);
    Order oa, ob;
    oa.station = a;
    ob.station = b;
    REQUIRE(cw.run(CmdSetOrders{bus, {oa, ob}}).ok);
    REQUIRE(cw.run(CmdStartStopVehicle{bus}).ok);

    const i32 top = speedUnits(cw.w.vehicles[bus].info().maxSpeedKmh);
    const i32 corner = std::max(10, top * 55 / 100);
    i32 fastest = 0;           // on the curves' tiles
    i32 slowest = INT32_MAX;   // once well into a curve
    i32 onCurveTicks = 0;
    std::set<TilePos> visited; // curve tiles driven over, following the curve
    i32 lastAlong = -1;
    RoadCurveId lastCurve;
    bool backwards = false;
    i64 visitsB = 0, lastVisit = -1;
    double worstOffLine = 0.0;
    for (i64 t = 0; t < 3 * kTicksPerDay; ++t) {
        cw.w.tick();
        const Vehicle& v = cw.w.vehicles[bus];
        if (cw.w.stations[b].lastVisitTick != lastVisit) {
            lastVisit = cw.w.stations[b].lastVisitTick;
            ++visitsB;
        }
        if (v.state != VehicleState::Driving) continue;
        const roadcurves::Place on = roadcurves::follow(cw.w, v.tile, v.entry, v.exit);
        if (!on.curve) {
            lastCurve = {};
            onCurveTicks = 0;
            continue;
        }
        visited.insert(v.tile);
        fastest = std::max(fastest, v.speed);
        if (++onCurveTicks > 6) slowest = std::min(slowest, v.speed);
        // Steadily on along the curve, whichever way the bus is going.
        const RoadCurveId id = roadcurves::at(cw.w, v.tile);
        const i32 along = on.begin() + (on.end() - on.begin()) * v.progress / std::max(1, std::abs(on.end() - on.begin()));
        const i32 fromStart = on.forward ? along : on.curve->length - along;
        if (id == lastCurve && fromStart < lastAlong) backwards = true;
        lastCurve = id;
        lastAlong = fromStart;
#if TP_TEST_RENDERING
        // Drawn in its lane beside the curve's centre line.
        if (const auto pose = gfx::vehiclePose(cw.w, v, 0.0)) {
            const TPoint p{static_cast<i32>(pose->pos.x * kTrackUnits), static_cast<i32>(pose->pos.z * kTrackUnits)};
            i64 nearest = INT64_MAX;
            for (const TrackSample& sm : on.curve->samples) nearest = std::min(nearest, distance(sm.p, p));
            worstOffLine = std::max(worstOffLine, std::abs(nearest / double(kTrackUnits) - gfx::kLaneOffset));
        }
#endif
    }
    const RoadCurve& bendCurve = cw.w.roadCurves[RoadCurveId(bend.createdId)];
    const RoadCurve& sCurve = cw.w.roadCurves[RoadCurveId(s.createdId)];
    CHECK(visitsB >= 2);
    CHECK(!backwards);
    CHECK(!cw.w.vehicles[bus].lost);
    // Every tile of both curves was driven along the curve (except a corner too small to stop on).
    for (const RoadCurve* c : {&bendCurve, &sCurve})
        for (usize i = 0; i < c->tiles.size(); ++i)
            if (c->tileStart[i + 1] - c->tileStart[i] > top) CHECK(visited.contains(c->tiles[i]));
    // Wide curves don't slow buses down to the speed of a street corner.
    CHECK(fastest <= top);
    CHECK(slowest != INT32_MAX);
    CHECK(slowest > corner + 4);
    CHECK(worstOffLine < 0.08);
}

TEST_CASE("towns: organic towns lay curved roads and leave some plots open") {
    WorldSettings s;
    s.seed = 77;
    World w(s);
    for (i32 y = -6; y <= 6; ++y)
        for (i32 x = -6; x <= 6; ++x) w.activateChunk({x, y});
    usize townCurves = 0;
    for (const auto [id, c] : w.roadCurves) {
        CHECK_EQ(c.owner, kOwnerTown);
        for (const TilePos t : c.tiles) CHECK(w.map.tile(t)->type == TileType::Road);
        ++townCurves;
    }
    CHECK(townCurves >= 5);
    // Road-side plots: open land beside a town road, level enough for a house.
    usize plots = 0, built = 0;
    for (const auto [id, t] : w.towns) {
        std::set<TilePos> seen;
        for (const TilePos r : t.roads)
            for (const Dir d : kDirs) {
                const TilePos p = neighbour(r, d);
                const Tile* tile = w.map.tile(p);
                if (!tile || !seen.insert(p).second) continue;
                if (tile->type == TileType::Building) ++built;
                else if (tile->type == TileType::Clear && towns::canPlaceBuilding(w, p, 1)) ++plots;
            }
    }
    std::printf("towns: %zu curves, %zu built plots, %zu open plots\n", townCurves, built, plots);
    CHECK(built > 0);
    CHECK(plots * 100 > (plots + built) * 12); // not every plot beside a road is built on
}

TEST_CASE("industries: founded well away from towns") {
    WorldSettings s;
    s.seed = 77;
    World w(s);
    for (i32 y = -8; y <= 8; ++y)
        for (i32 x = -8; x <= 8; ++x) w.activateChunk({x, y});
    REQUIRE(!w.industries.empty());
    for (const Industry& industry : w.industries) {
        const Building& b = w.buildings[industry.building.id];
        const i32 size = b.size();
        // No town house or town road within the distance, in any direction.
        for (i32 dy = -industries::kMinTownDistance; dy < size + industries::kMinTownDistance; ++dy)
            for (i32 dx = -industries::kMinTownDistance; dx < size + industries::kMinTownDistance; ++dx) {
                const Tile* t = w.map.tile({b.pos.x + dx, b.pos.y + dy});
                if (!t) continue;
                CHECK(!(t->type == TileType::Road && t->roadOwner == kOwnerTown));
                if (t->type == TileType::Building)
                    if (const Building* other = w.buildings.tryGet(BuildingId(t->index)))
                        CHECK(industries::isCountryside(other->type) || !other->town.valid());
            }
        for (const auto [id, t] : w.towns) {
            const i32 gx = std::max({b.pos.x - t.centre.x, t.centre.x - (b.pos.x + size - 1), 0});
            const i32 gy = std::max({b.pos.y - t.centre.y, t.centre.y - (b.pos.y + size - 1), 0});
            CHECK(std::max(gx, gy) >= industries::kMinTownDistance);
        }
    }
    // Asked for right in a town, none is founded there.
    for (const auto [id, town] : w.towns) {
        CHECK(!industries::found(w, industries::kSawmill, town.centre, 8).valid());
        break;
    }
}

TEST_CASE("industries: kept clear of town roads, houses and centres") {
    CurveWorld cw;
    REQUIRE(siteIsOpen(cw));
    World& w = cw.w;
    // No town is still to be founded nearby: only the town road and town placed here count.
    for (i32 cy = -2; cy <= 2; ++cy)
        for (i32 cx = -2; cx <= 2; ++cx) w.processedTownCells.insert(ChunkPos{cx, cy}.key());
    const auto gap = [](const Building& b, TilePos p) {
        const i32 gx = std::max({b.pos.x - p.x, p.x - (b.pos.x + b.size() - 1), 0});
        const i32 gy = std::max({b.pos.y - p.y, p.y - (b.pos.y + b.size() - 1), 0});
        return std::max(gx, gy);
    };
    // A lone town road: an industry asked for beside it isn't founded; one well away is.
    const TilePos road = cw.at(4, 20);
    Tile& rt = *w.map.tile(road);
    rt.type = TileType::Road;
    rt.road = dirBit(Dir::E) | dirBit(Dir::W);
    rt.roadOwner = kOwnerTown;
    CHECK(!industries::found(w, industries::kGrainFarm, {road.x + 6, road.y}, 0).valid());
    CHECK(!industries::found(w, industries::kGrainFarm, {road.x + 12, road.y}, 0).valid());
    const BuildingId far = industries::found(w, industries::kGrainFarm, {road.x + 24, road.y}, 0);
    REQUIRE(far.valid());
    CHECK(gap(w.buildings[far], road) >= industries::kMinTownDistance);
    // A town's centre: industries keep their distance beyond its built-up radius too.
    for (const auto [id, b] : w.buildings) towns::removeBuilding(w, id);
    w.industries.clear();
    rt = Tile{};
    const TownId town = w.towns.create();
    w.towns[town].centre = road;
    w.towns[town].radius = 6;
    CHECK(!industries::found(w, industries::kGrainFarm, {road.x + 18, road.y}, 0).valid());
    const BuildingId beyond = industries::found(w, industries::kGrainFarm, {road.x + 28, road.y}, 0);
    REQUIRE(beyond.valid());
    CHECK(gap(w.buildings[beyond], road) >= 6 + industries::kMinTownDistance);
}
