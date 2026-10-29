#include "sim/road_curves.hpp"

#include "core/math.hpp"
#include "sim/prices.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace tp {

namespace {

u64 tileKey(TilePos t) { return ChunkPos{t.x, t.y}.key(); }

// The side of tile `a` that faces its neighbour `b`.
Dir towards(TilePos a, TilePos b) { return b.x > a.x ? Dir::E : b.x < a.x ? Dir::W : b.y > a.y ? Dir::S : Dir::N; }

constexpr i64 kT = 65536; // curve parameter scale

TPoint lerp(TPoint a, TPoint b, i64 t) {
    return {static_cast<i32>(a.x + ((static_cast<i64>(b.x) - a.x) * t >> 16)),
            static_cast<i32>(a.z + ((static_cast<i64>(b.z) - a.z) * t >> 16))};
}

TPoint bezier(const TPoint (&p)[4], i64 t) {
    const TPoint q0 = lerp(p[0], p[1], t), q1 = lerp(p[1], p[2], t), q2 = lerp(p[2], p[3], t);
    return lerp(lerp(q0, q1, t), lerp(q1, q2, t), t);
}

// Radius of curvature of the cubic at parameter t (units; 0 where it runs straight).
i64 radiusAt(const TPoint (&p)[4], i64 t) {
    const i64 d0x = static_cast<i64>(p[1].x) - p[0].x, d0z = static_cast<i64>(p[1].z) - p[0].z;
    const i64 d1x = static_cast<i64>(p[2].x) - p[1].x, d1z = static_cast<i64>(p[2].z) - p[1].z;
    const i64 d2x = static_cast<i64>(p[3].x) - p[2].x, d2z = static_cast<i64>(p[3].z) - p[2].z;
    const i64 u = kT - t;
    const i64 w0 = u * u / kT, w1 = 2 * u * t / kT, w2 = t * t / kT;
    // First and second derivatives (units per whole curve).
    const i64 vx = 3 * (w0 * d0x + w1 * d1x + w2 * d2x) / kT, vz = 3 * (w0 * d0z + w1 * d1z + w2 * d2z) / kT;
    const i64 ax = 6 * (u * (d1x - d0x) + t * (d2x - d1x)) / kT, az = 6 * (u * (d1z - d0z) + t * (d2z - d1z)) / kT;
    const i64 cross = vx * az - vz * ax;
    if (cross == 0) return 0;
    const i64 speed = static_cast<i64>(isqrt(static_cast<u64>(vx * vx + vz * vz)));
    return std::max<i64>(1, speed * speed * speed / std::abs(cross));
}

void index(World& w, RoadCurveId id) {
    const RoadCurve& c = w.roadCurves[id];
    for (usize i = 0; i < c.tiles.size(); ++i) w.roadCurveTiles[tileKey(c.tiles[i])] = {id, static_cast<u16>(i)};
    for (const TilePos t : c.verge) ++w.roadCurveVerges[tileKey(t)];
}

void unindex(World& w, RoadCurveId id) {
    const RoadCurve& c = w.roadCurves[id];
    for (const TilePos t : c.tiles) w.roadCurveTiles.erase(tileKey(t));
    for (const TilePos t : c.verge)
        if (const auto it = w.roadCurveVerges.find(tileKey(t)); it != w.roadCurveVerges.end() && --it->second == 0)
            w.roadCurveVerges.erase(it);
}

const char* inTheWay(const Tile& t) {
    switch (t.type) {
    case TileType::Water: return "Can't build on water";
    case TileType::Building: return "Building in the way";
    case TileType::Station: return "Station in the way";
    case TileType::Depot: return "Depot in the way";
    case TileType::Rail:
    case TileType::Track: return "Railway in the way";
    case TileType::Footpath: return "Station footpath in the way";
    case TileType::Road: return "Road in the way";
    case TileType::Clear: break;
    }
    return "Can't build road here";
}

} // namespace

Dir RoadCurve::entrySide(usize i) const { return towards(tiles[i], i == 0 ? a : tiles[i - 1]); }
Dir RoadCurve::exitSide(usize i) const { return towards(tiles[i], i + 1 == tiles.size() ? b : tiles[i + 1]); }

namespace roadcurves {

TPoint edgePoint(TilePos t, Dir side) {
    const TPoint c = tileCentre(t);
    const TilePos o = dirOffset(side);
    return {c.x + o.x * kTrackUnits / 2, c.z + o.y * kTrackUnits / 2};
}

bool shape(RoadCurve& c, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    c.samples.clear();
    c.tileStart.clear();
    c.verge.clear();
    c.length = 0;
    c.minRadius = 0;
    if (c.a == c.b) return fail("Drag to another tile");
    const TPoint p0 = edgePoint(c.a, c.sideA), p3 = edgePoint(c.b, c.sideB);
    // Directions of travel at both ends, from a towards b.
    const TilePos u0 = dirOffset(c.sideA), u3 = dirOffset(opposite(c.sideB));
    const i64 cx = static_cast<i64>(p3.x) - p0.x, cz = static_cast<i64>(p3.z) - p0.z;
    const i64 along0 = cx * u0.x + cz * u0.y, along3 = cx * u3.x + cz * u3.y;
    if (u0 == TilePos{-u3.x, -u3.y}) return fail("A curve turns a quarter turn at most");
    if (along0 <= 0 || along3 <= 0) return fail("The road can't curve round to there");
    i64 h0 = 0, h3 = 0;
    if (u0 == u3) {
        if (cx * u0.y - cz * u0.x == 0) return fail("That's straight: use the road tool");
        h0 = h3 = along0 * 2 / 5; // a gentle S between parallel roads
    } else {
        // A quarter turn: the handles reach towards the corner where the two ends' lines meet.
        h0 = along0 * 141 / 256;
        h3 = along3 * 141 / 256;
    }
    const TPoint ctrl[4] = {p0,
                            {static_cast<i32>(p0.x + u0.x * h0), static_cast<i32>(p0.z + u0.y * h0)},
                            {static_cast<i32>(p3.x - u3.x * h3), static_cast<i32>(p3.z - u3.y * h3)},
                            p3};
    const i64 rough = distance(ctrl[0], ctrl[1]) + distance(ctrl[1], ctrl[2]) + distance(ctrl[2], ctrl[3]);
    if (rough > static_cast<i64>(kRoadCurveMaxTiles) * kTrackUnits * 3 / 2) return fail("Curve too long");
    const int count = static_cast<int>(std::clamp<i64>(rough / 12, 8, 8192));
    std::vector<TrackSample>& sm = c.samples;
    sm.resize(static_cast<usize>(count) + 1);
    i64 s16 = 0; // length so far in sixteenths of a unit (whole units would lose half a unit a step)
    i64 minR = 0;
    for (int i = 0; i <= count; ++i) {
        const i64 t = kT * i / count;
        const TPoint p = bezier(ctrl, t);
        if (i > 0) {
            const TPoint q = sm[static_cast<usize>(i) - 1].p;
            const i64 dx = static_cast<i64>(p.x) - q.x, dz = static_cast<i64>(p.z) - q.z;
            s16 += static_cast<i64>(isqrt(static_cast<u64>((dx * dx + dz * dz) * 256)));
        }
        sm[static_cast<usize>(i)] = {p, 0, static_cast<i32>(s16 / 16)};
        if (const i64 r = radiusAt(ctrl, t); r > 0 && (minR == 0 || r < minR)) minR = r;
    }
    const i64 s = s16 / 16;
    c.length = static_cast<i32>(s);
    c.minRadius = static_cast<i32>(std::min<i64>(minR, INT32_MAX));
    if (c.minRadius > 0 && c.minRadius < kRoadCurveMinRadius) return fail("Too tight: road curves need a radius of two tiles");

    // The tiles the centre line passes through, in order: a step to the next tile at each tile
    // edge it crosses (the x edge first where it passes exactly through a corner).
    std::vector<TilePos> tiles{neighbour(c.a, c.sideA)};
    std::vector<i32> starts{0};
    TilePos cur = tiles.front();
    for (usize i = 0; i + 1 < sm.size(); ++i) {
        const TPoint p = sm[i].p, q = sm[i + 1].p;
        const i64 dx = static_cast<i64>(q.x) - p.x, dz = static_cast<i64>(q.z) - p.z;
        const i64 adx = std::abs(dx), adz = std::abs(dz);
        for (int guard = 0; guard < 4; ++guard) {
            const i64 ax = dx > 0 ? static_cast<i64>(cur.x + 1) * kTrackUnits - p.x : static_cast<i64>(p.x) - cur.x * kTrackUnits;
            const i64 az = dz > 0 ? static_cast<i64>(cur.y + 1) * kTrackUnits - p.z : static_cast<i64>(p.z) - cur.y * kTrackUnits;
            const bool crossX = dx != 0 && ax >= 0 && ax < adx;
            const bool crossZ = dz != 0 && az >= 0 && az < adz;
            if (!crossX && !crossZ) break;
            const bool stepX = crossX && (!crossZ || ax * adz <= az * adx);
            const i64 along = stepX ? sm[i].s + (sm[i + 1].s - sm[i].s) * ax / adx : sm[i].s + (sm[i + 1].s - sm[i].s) * az / adz;
            cur = stepX ? TilePos{cur.x + (dx > 0 ? 1 : -1), cur.y} : TilePos{cur.x, cur.y + (dz > 0 ? 1 : -1)};
            tiles.push_back(cur);
            starts.push_back(static_cast<i32>(along));
            if (tiles.size() > static_cast<usize>(kRoadCurveMaxTiles)) return fail("Curve too long");
        }
    }
    if (cur != neighbour(c.b, c.sideB)) return fail("Can't fit a curve between these roads");
    if (tiles.size() < 2) return fail("Too short for a curve");
    std::vector<TilePos> sorted = tiles;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end() || std::binary_search(sorted.begin(), sorted.end(), c.a) ||
        std::binary_search(sorted.begin(), sorted.end(), c.b))
        return fail("The road can't curve round to there");
    starts.push_back(c.length);
    c.tiles = std::move(tiles);
    c.tileStart = std::move(starts);

    // The verge: tiles beside the chain that the road surface reaches over at a corner.
    const usize stride = static_cast<usize>(std::clamp<i64>(24 * count / std::max<i64>(1, s), 1, 64));
    for (usize i = 0; i < sm.size(); i += stride) {
        const TPoint before = sm[i > 0 ? i - 1 : 0].p, after = sm[std::min(i + 1, sm.size() - 1)].p;
        const TDir d = normalised(static_cast<i64>(after.x) - before.x, static_cast<i64>(after.z) - before.z);
        for (const i32 reach : {-kRoadCurveVerge, -kRoadCurveVerge / 2, kRoadCurveVerge / 2, kRoadCurveVerge}) {
            const TilePos t = tileAt({static_cast<i32>(sm[i].p.x - static_cast<i64>(d.z) * reach / kDirOne),
                                      static_cast<i32>(sm[i].p.z + static_cast<i64>(d.x) * reach / kDirOne)});
            if (t != c.a && t != c.b && !std::binary_search(sorted.begin(), sorted.end(), t)) c.verge.push_back(t);
        }
    }
    std::sort(c.verge.begin(), c.verge.end());
    c.verge.erase(std::unique(c.verge.begin(), c.verge.end()), c.verge.end());
    return true;
}

Dir naturalEndSide(TilePos a, Dir sideA, TilePos b) {
    const TPoint p0 = edgePoint(a, sideA), cb = tileCentre(b);
    const TilePos u = dirOffset(sideA), right = dirOffset(rotateCW(sideA));
    const i64 dx = static_cast<i64>(cb.x) - p0.x, dz = static_cast<i64>(cb.z) - p0.z;
    const i64 forward = dx * u.x + dz * u.y, lateral = dx * right.x + dz * right.y;
    if (std::abs(lateral) * 2 > forward) return opposite(lateral > 0 ? rotateCW(sideA) : rotateCCW(sideA));
    return opposite(sideA);
}

RoadCurvePlan plan(const World& w, TilePos a, Dir sideA, TilePos b, Dir sideB, u8 owner) {
    RoadCurvePlan pl;
    const auto fail = [&](std::string why) {
        pl.ok = false;
        pl.error = std::move(why);
        return pl;
    };
    RoadCurve& c = pl.curve;
    c.a = a;
    c.b = b;
    c.sideA = sideA;
    c.sideB = sideB;
    c.owner = owner;
    if (!w.map.tile(a) || !w.map.tile(b)) return fail("Area not explored");
    if (std::string why; !shape(c, &why)) return fail(why);

    // The ends: open land or a road (not a level crossing, a bridge or another curve part way).
    Money trees = 0;
    for (const auto& [tile, side] : {std::pair{a, sideA}, std::pair{b, sideB}}) {
        const Tile* t = w.map.tile(tile);
        if (t->span) return fail("Bridge or tunnel in the way");
        if (t->type != TileType::Clear && t->type != TileType::Road) return fail(inTheWay(*t));
        if (t->type == TileType::Road && (t->rail || track::isCrossing(w, tile))) return fail("Roads can't branch off at a level crossing");
        if (at(w, tile).valid()) return fail("Roads join a curve at its ends only");
        if (roadPieces(*t) & dirBit(side)) return fail("Already built");
        if (onVerge(w, tile)) return fail("Too close to another curved road");
        if (w.trackTiles.contains(tileKey(tile))) return fail("Railway in the way");
        if (t->type == TileType::Clear) trees += t->trees;
    }
    for (const TilePos tile : c.tiles) {
        const Tile* t = w.map.tile(tile);
        if (!t) return fail("Area not explored");
        if (t->span) return fail("Bridge or tunnel in the way");
        if (t->type != TileType::Clear) return fail(inTheWay(*t));
        if (w.trackTiles.contains(tileKey(tile))) return fail("Railway in the way");
        if (onVerge(w, tile)) return fail("Too close to another curved road");
        trees += t->trees;
    }
    for (const TilePos tile : c.verge) {
        const Tile* t = w.map.tile(tile);
        if (!t) return fail("Area not explored");
        if (t->span || (t->type != TileType::Clear && t->type != TileType::Water)) return fail(inTheWay(*t));
        if (w.trackTiles.contains(tileKey(tile))) return fail("Railway in the way");
        if (t->type == TileType::Clear) trees += t->trees;
    }

    // Every tile of the chain must take its pieces on its slope, meeting its neighbours at the
    // same height (the end tiles keep the way they sit).
    const usize n = c.tiles.size();
    std::vector<TilePos> chain{a};
    chain.insert(chain.end(), c.tiles.begin(), c.tiles.end());
    chain.push_back(b);
    std::vector<Surface> surfaces(chain.size());
    for (usize k = 0; k < chain.size(); ++k) {
        const Tile& t = *w.map.tile(chain[k]);
        const Slope slope = w.map.slope(chain[k]);
        u8 pieces = 0;
        if (k == 0) pieces = static_cast<u8>(allRoadPieces(t) | dirBit(sideA));
        else if (k == n + 1) pieces = static_cast<u8>(allRoadPieces(t) | dirBit(sideB));
        else pieces = static_cast<u8>(dirBit(c.entrySide(k - 1)) | dirBit(c.exitSide(k - 1)));
        surfaces[k] = roadSurface(slope, pieces);
        if (surfaces[k] == Surface::Invalid) return fail("Land sloped in the wrong direction");
        if ((k == 0 || k == n + 1) && allRoadPieces(t) && roadSurface(slope, allRoadPieces(t)) != surfaces[k])
            return fail("Can't join the road there on this slope");
    }
    for (usize k = 0; k + 1 < chain.size(); ++k) {
        const Dir d = towards(chain[k], chain[k + 1]);
        if (roadEdgeHalfLevels(w.map.corners(chain[k]), surfaces[k], d) !=
            roadEdgeHalfLevels(w.map.corners(chain[k + 1]), surfaces[k + 1], opposite(d)))
            return fail("The ground is too uneven here - level it first");
    }

    const Money tilesLong = (c.length + kTrackUnits - 1) / kTrackUnits;
    pl.cost = tilesLong * prices::kRoadCurvePerTile + 2 * prices::kRoadPiece + trees * prices::kRemoveTree;
    pl.ok = true;
    return pl;
}

RoadCurveId build(World& w, const RoadCurvePlan& pl) {
    const RoadCurve& c = pl.curve;
    for (usize i = 0; i < c.tiles.size(); ++i) {
        Tile& t = *w.map.tile(c.tiles[i]);
        t.type = TileType::Road;
        t.trees = 0;
        t.road = static_cast<u8>(dirBit(c.entrySide(i)) | dirBit(c.exitSide(i)));
        t.roadOwner = c.owner;
        t.owner = kOwnerNone;
        if (t.ground == Ground::Rock || t.ground == Ground::Rough) t.ground = Ground::Grass;
        w.map.touch(c.tiles[i]);
    }
    addRoadPiece(w.map, c.a, c.sideA, c.owner);
    addRoadPiece(w.map, c.b, c.sideB, c.owner);
    for (const TilePos v : c.verge)
        if (Tile* t = w.map.tile(v); t && t->type == TileType::Clear && t->trees) {
            t->trees = 0;
            w.map.touch(v);
        }
    const RoadCurveId id = w.roadCurves.create(c);
    index(w, id);
    ++w.roadCurveRevision;
    return id;
}

void remove(World& w, RoadCurveId id) {
    if (!w.roadCurves.contains(id)) return;
    unindex(w, id);
    const RoadCurve c = w.roadCurves[id];
    w.roadCurves.destroy(id);
    for (const TilePos tile : c.tiles) {
        Tile* t = w.map.tile(tile);
        if (!t || t->type != TileType::Road) continue;
        t->type = TileType::Clear;
        t->road = 0;
        t->roadOwner = kOwnerNone;
        t->owner = kOwnerNone;
        w.map.touch(tile);
    }
    for (const auto& [tile, side] : {std::pair{c.a, c.sideA}, std::pair{c.b, c.sideB}}) {
        Tile* t = w.map.tile(tile);
        if (!t || t->type != TileType::Road || !(roadPieces(*t) & dirBit(side))) continue;
        t->road = static_cast<u8>(t->road & ~dirBit(side));
        if (roadPieces(*t) == 0) t->roadOwner = kOwnerNone;
        if (t->road == 0) {
            t->type = TileType::Clear;
            t->owner = kOwnerNone;
        }
        w.map.touch(tile);
    }
    ++w.roadCurveRevision;
}

Money removalCost(const RoadCurve& c) { return static_cast<Money>(c.tiles.size() * 2 + 2) * prices::kRemoveRoadPiece; }

RoadCurveId at(const World& w, TilePos t) {
    const auto it = w.roadCurveTiles.find(tileKey(t));
    return it == w.roadCurveTiles.end() ? RoadCurveId{} : it->second.curve;
}

bool onVerge(const World& w, TilePos t) { return w.roadCurveVerges.contains(tileKey(t)); }

Place follow(const World& w, TilePos t, Dir entry, Dir exit) {
    const auto it = w.roadCurveTiles.find(tileKey(t));
    if (it == w.roadCurveTiles.end()) return {};
    const RoadCurve* c = w.roadCurves.tryGet(it->second.curve);
    const usize i = it->second.index;
    if (!c || i >= c->tiles.size() || c->tileStart.size() != c->tiles.size() + 1) return {};
    const Dir in = c->entrySide(i), out = c->exitSide(i);
    if (entry == in && exit == out) return {c, i, true};
    if (entry == out && exit == in) return {c, i, false};
    return {};
}

std::optional<Dir> onwardSide(const World& w, TilePos t, Dir entry) {
    const auto it = w.roadCurveTiles.find(tileKey(t));
    if (it == w.roadCurveTiles.end()) return std::nullopt;
    const RoadCurve* c = w.roadCurves.tryGet(it->second.curve);
    const usize i = it->second.index;
    if (!c || i >= c->tiles.size()) return std::nullopt;
    if (entry == c->entrySide(i)) return c->exitSide(i);
    if (entry == c->exitSide(i)) return c->entrySide(i);
    return std::nullopt;
}

i32 pathLength(const World& w, TilePos t, Dir entry, Dir exit) {
    if (const Place p = follow(w, t, entry, exit); p.curve) return std::max(1, std::abs(p.end() - p.begin()));
    return tp::pathLength(entry, exit);
}

TPoint pointAt(const RoadCurve& c, i32 s, TDir* dir) {
    const std::vector<TrackSample>& sm = c.samples;
    if (sm.size() < 2) return {};
    s = std::clamp(s, 0, c.length);
    const auto it = std::upper_bound(sm.begin(), sm.end(), s, [](i32 v, const TrackSample& x) { return v < x.s; });
    const usize i = std::min<usize>(static_cast<usize>(std::max<std::ptrdiff_t>(it - sm.begin(), 1)) - 1, sm.size() - 2);
    const TrackSample& p = sm[i];
    const TrackSample& q = sm[i + 1];
    const i64 span = std::max(1, q.s - p.s);
    if (dir) *dir = normalised(static_cast<i64>(q.p.x) - p.p.x, static_cast<i64>(q.p.z) - p.p.z);
    return {static_cast<i32>(p.p.x + (static_cast<i64>(q.p.x) - p.p.x) * (s - p.s) / span),
            static_cast<i32>(p.p.z + (static_cast<i64>(q.p.z) - p.p.z) * (s - p.s) / span)};
}

i32 speedLimit(const RoadCurve& c, i32 maxSpeed) {
    constexpr i32 kOpen = 4 * kTrackUnits; // curves this wide are taken at full speed
    if (c.minRadius <= 0 || c.minRadius >= kOpen) return maxSpeed;
    // Grid corners (a radius of half a tile) are taken at 55% of full speed.
    const i32 r = std::max(c.minRadius, kTrackUnits / 2);
    const i32 percent = 55 + 45 * (r - kTrackUnits / 2) / (kOpen - kTrackUnits / 2);
    return std::max(10, maxSpeed * percent / 100);
}

void rebuildDerived(World& w) {
    w.roadCurveTiles.clear();
    w.roadCurveVerges.clear();
    for (auto [id, c] : w.roadCurves) {
        const std::vector<TilePos> saved = c.tiles;
        shape(c);
        if (c.tiles != saved || c.tileStart.size() != saved.size() + 1) {
            // Laid out differently when built: keep its tiles, sharing the length out evenly.
            c.tiles = saved;
            c.tileStart.clear();
            for (usize i = 0; i <= saved.size(); ++i)
                c.tileStart.push_back(static_cast<i32>(static_cast<i64>(c.length) * static_cast<i64>(i) / std::max<usize>(1, saved.size())));
        }
        index(w, id);
    }
    ++w.roadCurveRevision;
}

} // namespace roadcurves

} // namespace tp
