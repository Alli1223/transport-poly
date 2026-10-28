#include "sim/track.hpp"

#include "core/math.hpp"
#include "sim/passengers.hpp"
#include "sim/prices.hpp"
#include "sim/rail.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <optional>
#include <unordered_set>

namespace tp {

TDir normalised(i64 x, i64 z) {
    const i64 len = static_cast<i64>(isqrt(static_cast<u64>(x * x + z * z)));
    if (len == 0) return TDir{0, 0};
    return TDir{static_cast<i32>(x * kDirOne / len), static_cast<i32>(z * kDirOne / len)};
}

i64 distance(TPoint a, TPoint b) {
    const i64 dx = static_cast<i64>(b.x) - a.x, dz = static_cast<i64>(b.z) - a.z;
    return static_cast<i64>(isqrt(static_cast<u64>(dx * dx + dz * dz)));
}

namespace track {

namespace {

constexpr i64 kT = 65536; // curve parameter scale

struct Curve {
    TPoint p[4];
};

TPoint lerp(TPoint a, TPoint b, i64 t) {
    return {static_cast<i32>(a.x + ((static_cast<i64>(b.x) - a.x) * t >> 16)),
            static_cast<i32>(a.z + ((static_cast<i64>(b.z) - a.z) * t >> 16))};
}

// Polar form (blossom) of the cubic: blossom(t, t, t) is the point at t, and the control points
// of the part of the curve between a and b are blossom(a,a,a), (a,a,b), (a,b,b), (b,b,b).
TPoint blossom(const Curve& c, i64 u, i64 v, i64 w) {
    const TPoint q0 = lerp(c.p[0], c.p[1], u), q1 = lerp(c.p[1], c.p[2], u), q2 = lerp(c.p[2], c.p[3], u);
    const TPoint r0 = lerp(q0, q1, v), r1 = lerp(q1, q2, v);
    return lerp(r0, r1, w);
}

TPoint at(const Curve& c, i64 t) { return blossom(c, t, t, t); }

// Cubic Hermite height profile over normalised distance u in [0, kT].
i64 hermite(i64 u, i64 h0, i64 t0, i64 h1, i64 t1) {
    const i64 u2 = u * u / kT, u3 = u2 * u / kT;
    const i64 b00 = 2 * u3 - 3 * u2 + kT, b10 = u3 - 2 * u2 + u, b01 = -2 * u3 + 3 * u2, b11 = u3 - u2;
    return (b00 * h0 + b10 * t0 + b01 * h1 + b11 * t1) / kT;
}

// Derivative of the profile with respect to u (fine units per whole length).
i64 hermiteDeriv(i64 u, i64 h0, i64 t0, i64 h1, i64 t1) {
    const i64 u2 = u * u / kT;
    const i64 d00 = 6 * u2 - 6 * u, d10 = 3 * u2 - 4 * u + kT, d01 = -6 * u2 + 6 * u, d11 = 3 * u2 - 2 * u;
    return (d00 * h0 + d10 * t0 + d01 * h1 + d11 * t1) / kT;
}

// Samples a curve: positions at even parameter steps, distances along it and a height profile
// from h0 to h1 with the given grades (along the direction of travel) at the ends.
std::vector<TrackSample> sampleCurve(const Curve& c, i32 h0, i64 g0, i32 h1, i64 g1, int count) {
    std::vector<TrackSample> out(static_cast<usize>(count + 1));
    i64 s = 0;
    for (int i = 0; i <= count; ++i) {
        const TPoint p = at(c, kT * i / count);
        if (i > 0) s += distance(out[static_cast<usize>(i - 1)].p, p);
        out[static_cast<usize>(i)] = {p, 0, static_cast<i32>(s)};
    }
    const i64 len = std::max<i64>(1, s);
    const i64 t0 = g0 * len / kGradeOne, t1 = g1 * len / kGradeOne;
    for (TrackSample& sm : out) sm.h = static_cast<i32>(hermite(sm.s * kT / len, h0, t0, h1, t1));
    return out;
}

int sampleCount(const Curve& c) {
    const i64 rough = distance(c.p[0], c.p[1]) + distance(c.p[1], c.p[2]) + distance(c.p[2], c.p[3]);
    return static_cast<int>(std::clamp<i64>(rough / 32, 4, 1024));
}

// Distance from p to the polyline, and the rail height at the nearest point.
struct Nearest {
    i64 d = INT64_MAX;
    i32 h = 0;
    i32 i = -1; // the segment (from sample i to i + 1)
    TPoint q{}; // the nearest point (set by nearest() only)
};
Nearest nearest(const std::vector<TrackSample>& line, TPoint p) {
    Nearest best;
    for (usize i = 0; i + 1 < line.size(); ++i) {
        const TrackSample& a = line[i];
        const TrackSample& b = line[i + 1];
        const i64 vx = static_cast<i64>(b.p.x) - a.p.x, vz = static_cast<i64>(b.p.z) - a.p.z;
        const i64 wx = static_cast<i64>(p.x) - a.p.x, wz = static_cast<i64>(p.z) - a.p.z;
        const i64 len2 = vx * vx + vz * vz;
        i64 num = vx * wx + vz * wz;
        num = len2 == 0 ? 0 : std::clamp<i64>(num, 0, len2);
        const i64 qx = len2 ? a.p.x + vx * num / len2 : a.p.x, qz = len2 ? a.p.z + vz * num / len2 : a.p.z;
        const i64 dx = p.x - qx, dz = p.z - qz;
        const i64 d = static_cast<i64>(isqrt(static_cast<u64>(dx * dx + dz * dz)));
        if (d < best.d) {
            best.d = d;
            best.h = static_cast<i32>(len2 ? a.h + (static_cast<i64>(b.h) - a.h) * num / len2 : a.h);
            best.i = static_cast<i32>(i);
            best.q = {static_cast<i32>(qx), static_cast<i32>(qz)};
        }
    }
    return best;
}

// Nearest distance and rail height for every grid point (gx * kTrackUnits + offset) within
// `reach` of the polyline. Each segment only visits the points around it.
struct Field {
    i32 gx0 = 0, gy0 = 0, w = 0, h = 0;
    std::vector<Nearest> cells;
    const Nearest* at(i32 gx, i32 gy) const {
        if (gx < gx0 || gy < gy0 || gx >= gx0 + w || gy >= gy0 + h) return nullptr;
        return &cells[static_cast<usize>((gy - gy0) * w + (gx - gx0))];
    }
};

i32 floorDivUnits(i64 v) { return static_cast<i32>(v >= 0 ? v / kTrackUnits : (v - kTrackUnits + 1) / kTrackUnits); }

Field nearestField(const std::vector<TrackSample>& line, i32 reach, i32 offset) {
    Field f;
    if (line.size() < 2) return f;
    i64 x0 = INT64_MAX, z0 = INT64_MAX, x1 = INT64_MIN, z1 = INT64_MIN;
    for (const TrackSample& sm : line) {
        x0 = std::min<i64>(x0, sm.p.x);
        z0 = std::min<i64>(z0, sm.p.z);
        x1 = std::max<i64>(x1, sm.p.x);
        z1 = std::max<i64>(z1, sm.p.z);
    }
    f.gx0 = floorDivUnits(x0 - reach - offset);
    f.gy0 = floorDivUnits(z0 - reach - offset);
    f.w = floorDivUnits(x1 + reach - offset) + 2 - f.gx0;
    f.h = floorDivUnits(z1 + reach - offset) + 2 - f.gy0;
    f.cells.assign(static_cast<usize>(f.w) * static_cast<usize>(f.h), Nearest{});
    for (usize i = 0; i + 1 < line.size(); ++i) {
        const TrackSample& a = line[i];
        const TrackSample& b = line[i + 1];
        const i64 vx = static_cast<i64>(b.p.x) - a.p.x, vz = static_cast<i64>(b.p.z) - a.p.z;
        const i64 len2 = vx * vx + vz * vz;
        const i32 gxa = std::max(f.gx0, floorDivUnits(std::min(a.p.x, b.p.x) - reach - offset));
        const i32 gxb = std::min(f.gx0 + f.w - 1, floorDivUnits(std::max(a.p.x, b.p.x) + reach - offset) + 1);
        const i32 gya = std::max(f.gy0, floorDivUnits(std::min(a.p.z, b.p.z) - reach - offset));
        const i32 gyb = std::min(f.gy0 + f.h - 1, floorDivUnits(std::max(a.p.z, b.p.z) + reach - offset) + 1);
        for (i32 gy = gya; gy <= gyb; ++gy) {
            for (i32 gx = gxa; gx <= gxb; ++gx) {
                const i64 px = static_cast<i64>(gx) * kTrackUnits + offset, pz = static_cast<i64>(gy) * kTrackUnits + offset;
                const i64 wx = px - a.p.x, wz = pz - a.p.z;
                const i64 num = len2 == 0 ? 0 : std::clamp<i64>(vx * wx + vz * wz, 0, len2);
                const i64 qx = len2 ? a.p.x + vx * num / len2 : a.p.x, qz = len2 ? a.p.z + vz * num / len2 : a.p.z;
                const i64 dx = px - qx, dz = pz - qz;
                const i64 d2 = dx * dx + dz * dz;
                if (d2 > static_cast<i64>(reach) * reach) continue;
                Nearest& cell = f.cells[static_cast<usize>((gy - f.gy0) * f.w + (gx - f.gx0))];
                const i64 d = static_cast<i64>(isqrt(static_cast<u64>(d2)));
                if (d < cell.d) {
                    cell.d = d;
                    cell.h = static_cast<i32>(len2 ? a.h + (static_cast<i64>(b.h) - a.h) * num / len2 : a.h);
                    cell.i = static_cast<i32>(i);
                }
            }
        }
    }
    return f;
}

Curve edgeCurve(const World& w, const TrackEdge& e) {
    const TrackNode& a = w.trackNodes[e.a];
    const TrackNode& b = w.trackNodes[e.b];
    return Curve{{a.pos, offset(a.pos, a.side(e.sideA), e.handleA), offset(b.pos, b.side(e.sideB), e.handleB), b.pos}};
}

u64 tileKey(TilePos t) { return ChunkPos{t.x, t.y}.key(); }

constexpr i32 kCrossingReach = 96; // the centre line passes this close to the middle of a crossed road tile
constexpr i32 kGrazeReach = 160;   // a road tile farther than this from the centre line is only grazed

// A tile under a sampled track: what carries the rails nearest to it, and how near they pass
// its middle.
struct BedTile {
    TilePos t;
    TrackStructure kind = TrackStructure::Ground;
    Nearest near;
};

i32 segmentMid(const std::vector<TrackSample>& sm, i32 i) {
    return (sm[static_cast<usize>(i)].s + sm[static_cast<usize>(i) + 1].s) / 2;
}

std::vector<BedTile> bedTileList(const std::vector<TrackSample>& samples, const std::vector<TrackSpan>& spans, i32 halfWidth) {
    std::vector<BedTile> out;
    const i32 reach = halfWidth + kTrackUnits / 2;
    const Field f = nearestField(samples, reach, kTrackUnits / 2);
    for (i32 y = 0; y < f.h; ++y)
        for (i32 x = 0; x < f.w; ++x) {
            const Nearest& c = f.cells[static_cast<usize>(y * f.w + x)];
            if (c.d > reach) continue;
            out.push_back({{f.gx0 + x, f.gy0 + y}, track::structureAt(spans, segmentMid(samples, c.i)), c});
        }
    return out;
}

// Bridges and tunnels from what carries each sample: scraps of ground shorter than a tile
// between two bridges (or two tunnels) join them; bridges shorter than a tile that cross
// nothing, and tunnels shorter than two tiles, become embankments and cuttings. Tunnels then
// reach out to where the hillside is about as high as their portals (`cover`: ground above
// the rails at each sample).
std::vector<TrackSpan> tidyStructures(const std::vector<TrackSample>& sm, std::vector<TrackStructure>& kinds,
                                      const std::vector<u8>& mustBridge, const std::vector<i32>& cover) {
    struct Run {
        usize a, b; // samples a..b
        TrackStructure kind;
    };
    const auto runs = [&] {
        std::vector<Run> out;
        for (usize i = 0; i < kinds.size();) {
            usize j = i;
            while (j + 1 < kinds.size() && kinds[j + 1] == kinds[i]) ++j;
            out.push_back({i, j, kinds[i]});
            i = j + 1;
        }
        return out;
    };
    const auto length = [&](const Run& r) { return sm[r.b].s - sm[r.a].s; };
    std::vector<Run> rs = runs();
    for (usize k = 1; k + 1 < rs.size(); ++k)
        if (rs[k].kind == TrackStructure::Ground && rs[k - 1].kind == rs[k + 1].kind && length(rs[k]) < kTrackUnits)
            for (usize i = rs[k].a; i <= rs[k].b; ++i) kinds[i] = rs[k - 1].kind;
    for (const Run& r : runs()) {
        if (r.kind == TrackStructure::Ground) continue;
        bool needed = false;
        for (usize i = r.a; i <= r.b; ++i) needed = needed || mustBridge[i];
        const i32 shortest = r.kind == TrackStructure::Bridge ? kTrackUnits : 2 * kTrackUnits;
        if (!needed && length(r) < shortest)
            for (usize i = r.a; i <= r.b; ++i) kinds[i] = TrackStructure::Ground;
    }
    constexpr i32 kPortalCover = 20;
    for (const Run& r : runs()) {
        if (r.kind != TrackStructure::Tunnel) continue;
        for (usize i = r.a; i-- > 0 && kinds[i] == TrackStructure::Ground && cover[i] >= kPortalCover;)
            kinds[i] = TrackStructure::Tunnel;
        for (usize i = r.b + 1; i < kinds.size() && kinds[i] == TrackStructure::Ground && cover[i] >= kPortalCover; ++i)
            kinds[i] = TrackStructure::Tunnel;
    }
    std::vector<TrackSpan> spans;
    for (const Run& r : runs())
        if (r.kind != TrackStructure::Ground) spans.push_back({sm[r.a].s, sm[r.b].s, r.kind});
    return spans;
}

// A road tile track may cross on the level: a straight road without junctions, level across
// its width (it may climb along its length). Gives the road's axis (true: north-south).
std::optional<bool> crossableRoad(const World& w, TilePos t) {
    const Tile* tile = w.map.tile(t);
    if (!tile || tile->type != TileType::Road || tile->rail || tile->span) return std::nullopt;
    const u8 bits = allRoadPieces(*tile);
    const bool ns = bits == (dirBit(Dir::N) | dirBit(Dir::S));
    const bool ew = bits == (dirBit(Dir::E) | dirBit(Dir::W));
    const FineCorners fc = w.map.fineCorners(t);
    if (ns && fc.h[0] == fc.h[1] && fc.h[3] == fc.h[2]) return true;
    if (ew && fc.h[0] == fc.h[3] && fc.h[1] == fc.h[2]) return false;
    return std::nullopt;
}

void computeCrossings(const World& w, TrackEdge& e) {
    e.crossings.clear();
    if (e.kind != TrackKind::Plain) return;
    for (const BedTile& bt : bedTileList(e.samples, e.spans, kTrackBedHalfWidth)) {
        if (bt.kind != TrackStructure::Ground || bt.near.d > kCrossingReach) continue;
        if (const Tile* tile = w.map.tile(bt.t); tile && tile->type == TileType::Road) e.crossings.push_back(bt.t);
    }
}

// A node lying (almost) exactly at p.
TrackNodeId nodeAt(const World& w, TPoint p) {
    for (const auto [id, n] : w.trackNodes)
        if (distance(n.pos, p) <= kTrackJoinRadius) return id;
    return {};
}

bool nodeOwnedByOther(const World& w, const TrackNode& n, CompanyId company) {
    for (const TrackEdgeId e : n.edges)
        if (e.valid() && w.trackEdges[e].owner != company) return true;
    return false;
}

// Mirror image of d about the line along unit vector c (the arc's end direction).
TDir reflect(TDir d, TDir c) {
    const i64 k = 2 * dot(d, c) / kDirOne;
    return normalised(k * c.x / kDirOne - d.x, k * c.z / kDirOne - d.z);
}

} // namespace

int sideFor(const TrackNode& node, TDir into) {
    // Within 30 degrees of one of the node's sides.
    constexpr i64 kCos30 = static_cast<i64>(kDirOne) * kDirOne * 866 / 1000;
    for (u8 s = 0; s < 2; ++s)
        if (dot(node.side(s), into) >= kCos30) return s;
    return -1;
}

i32 terrainAt(const World& w, TPoint p) {
    const TilePos t = tileAt(p);
    const i64 fx = p.x - static_cast<i64>(t.x) * kTrackUnits, fy = p.z - static_cast<i64>(t.y) * kTrackUnits;
    const FineCorners c = w.map.fineCorners(t);
    const i64 nw = c.h[0], ne = c.h[1], se = c.h[2], sw = c.h[3];
    constexpr i64 u = kTrackUnits;
    if (!c.splitNESW()) {
        if (fx >= fy) return static_cast<i32>(nw + (ne - nw) * fx / u + (se - ne) * fy / u);
        return static_cast<i32>(nw + (se - sw) * fx / u + (sw - nw) * fy / u);
    }
    if (fx + fy <= u) return static_cast<i32>(nw + (ne - nw) * fx / u + (sw - nw) * fy / u);
    return static_cast<i32>(se + (sw - se) * (u - fx) / u + (ne - se) * (u - fy) / u);
}

std::vector<TilePos> bedTiles(const std::vector<TrackSample>& samples, i32 halfWidth) {
    // A tile is under the bed when the centre line passes within the half width of the tile's
    // square (approximated by its centre plus half a tile).
    std::vector<TilePos> tiles;
    const i32 reach = halfWidth + kTrackUnits / 2;
    const Field f = nearestField(samples, reach, kTrackUnits / 2);
    for (i32 y = 0; y < f.h; ++y)
        for (i32 x = 0; x < f.w; ++x)
            if (f.cells[static_cast<usize>(y * f.w + x)].d <= reach) tiles.push_back({f.gx0 + x, f.gy0 + y});
    return tiles;
}

TrackStructure structureAt(const std::vector<TrackSpan>& spans, i32 s) {
    for (const TrackSpan& sp : spans)
        if (s >= sp.s0 && s <= sp.s1) return sp.kind;
    return TrackStructure::Ground;
}

void edgeTiles(const TrackEdge& e, std::vector<TilePos>* ground, std::vector<TilePos>* all) {
    for (const BedTile& bt : bedTileList(e.samples, e.spans, halfWidthOf(e.kind))) {
        if (all) all->push_back(bt.t);
        if (ground && bt.kind == TrackStructure::Ground) ground->push_back(bt.t);
    }
}

bool isCrossing(const World& w, TilePos t) {
    const auto it = w.trackTiles.find(tileKey(t));
    if (it == w.trackTiles.end()) return false;
    for (const TrackEdgeId id : it->second)
        if (std::ranges::find(w.trackEdges[id].crossings, t) != w.trackEdges[id].crossings.end()) return true;
    return false;
}

bool crossingClosed(const World& w, TilePos t) {
    const auto it = w.trackTiles.find(tileKey(t));
    if (it == w.trackTiles.end()) return false;
    for (const TrackEdgeId id : it->second) {
        const TrackEdge& e = w.trackEdges[id];
        if (e.reservedBy.valid() && std::ranges::find(e.crossings, t) != e.crossings.end()) return true;
    }
    return false;
}

void computeSamples(const World& w, TrackEdge& e) {
    const TrackNode& a = w.trackNodes[e.a];
    const TrackNode& b = w.trackNodes[e.b];
    const Curve c = edgeCurve(w, e);
    // Grades along the direction of travel from a to b.
    const i64 g0 = e.sideA == 0 ? a.grade : -a.grade;
    const i64 g1 = e.sideB == 0 ? -b.grade : b.grade;
    e.samples = sampleCurve(c, a.h, g0, b.h, g1, sampleCount(c));
    e.length = e.samples.back().s;
    e.minRadius = 0;
    const usize n = e.samples.size();
    const usize k = static_cast<usize>(std::max<i64>(1, kTrackUnits / 2 * static_cast<i64>(n - 1) / std::max<i64>(1, e.length)));
    for (usize i = k; i + k < n; ++i) {
        const TPoint o = e.samples[i - k].p, p = e.samples[i].p, q = e.samples[i + k].p;
        const i64 ax = static_cast<i64>(p.x) - o.x, az = static_cast<i64>(p.z) - o.z;
        const i64 bx = static_cast<i64>(q.x) - p.x, bz = static_cast<i64>(q.z) - p.z;
        const i64 cross = std::abs(ax * bz - az * bx);
        const i64 la = distance(o, p), lb = distance(p, q);
        if (cross * 2048 > la * lb) {
            const i64 r = (la + lb) * la * lb / (2 * cross);
            if (e.minRadius == 0 || r < e.minRadius) e.minRadius = static_cast<i32>(std::min<i64>(r, INT_MAX));
        }
    }
    computeCrossings(w, e);
}

i32 halfWidthOf(TrackKind kind) { return kind == TrackKind::Platform ? kPlatformHalfWidth : kTrackBedHalfWidth; }

void rebuildDerived(World& w) {
    w.trackTiles.clear();
    for (auto [id, e] : w.trackEdges) {
        computeSamples(w, e);
        std::vector<TilePos> all;
        edgeTiles(e, nullptr, &all);
        for (const TilePos t : all) w.trackTiles[tileKey(t)].push_back(id);
    }
    ++w.trackRevision;
}

TrackNodeId nodeNear(const World& w, TPoint p, i32 radius, bool anySide) {
    TrackNodeId best;
    i64 bestScore = INT64_MAX;
    for (const auto [id, n] : w.trackNodes) {
        const i64 d = distance(n.pos, p);
        if (d > radius) continue;
        const bool end = n.count(0) == 0 || n.count(1) == 0;
        if (!anySide && !n.hasRoom(0) && !n.hasRoom(1)) continue;
        // Prefer open track ends over branching off a through node.
        const i64 score = d + (end ? 0 : radius);
        if (score < bestScore) {
            bestScore = score;
            best = id;
        }
    }
    return best;
}

TrackEdgeId edgeNear(const World& w, TPoint p, i32 radius) {
    TrackEdgeId best;
    i64 bestD = INT64_MAX;
    for (const auto [id, e] : w.trackEdges) {
        const Nearest n = nearest(e.samples, p);
        if (n.d <= radius && n.d < bestD) {
            bestD = n.d;
            best = id;
        }
    }
    return best;
}

TrackPlan plan(const World& w, CompanyId company, const TrackAnchor& from, const TrackAnchor& to,
               const TrackOptions& options) {
    TrackPlan pl;
    pl.options = options;
    const auto fail = [&](std::string why) {
        pl.ok = false;
        pl.error = std::move(why);
        return pl;
    };
    struct End {
        bool existing = false;
        TrackNodeId id;
        u8 side = 0;
        TPoint pos;
        i32 h = 0;
        i64 grade = 0; // along `into`
        TDir into{0, 0};
    };
    const auto resolve = [&](const TrackAnchor& a, End& e) -> std::string {
        TrackNodeId id = a.node.valid() ? a.node : nodeAt(w, a.pos);
        if (!id.valid()) {
            e.pos = a.pos;
            e.into = a.into.isZero() ? TDir{0, 0} : normalised(a.into.x, a.into.z);
            return {};
        }
        const TrackNode* n = w.trackNodes.tryGet(id);
        if (!n) return "That track no longer exists";
        if (nodeOwnedByOther(w, *n, company)) return "Owned by another company";
        int s;
        if (a.into.isZero()) s = n->count(0) <= n->count(1) ? 0 : 1;
        else s = sideFor(*n, a.into);
        if (s < 0) return "Track can only continue in line with the existing track";
        if (!n->hasRoom(static_cast<u8>(s))) return "No room for another branch here";
        e.existing = true;
        e.id = id;
        e.side = static_cast<u8>(s);
        e.pos = n->pos;
        e.h = n->h;
        e.into = n->side(e.side);
        e.grade = s == 0 ? n->grade : -n->grade;
        return {};
    };
    End a, b;
    if (std::string why = resolve(from, a); !why.empty()) return fail(why);
    if (std::string why = resolve(to, b); !why.empty()) return fail(why);
    if (a.existing && b.existing && a.id == b.id) return fail("Drag to another point");
    const i64 chord = distance(a.pos, b.pos);
    if (chord < kTrackUnits / 2) return fail("Too short");
    if (chord > kTrackMaxLength) return fail("Too long - build it in shorter sections");
    for (const TilePos t : {tileAt(a.pos), tileAt(b.pos)})
        if (!w.map.tile(t)) return fail("Area not explored");

    // Directions of travel at both ends. A free end follows a circular arc from the other end.
    const TDir c = normalised(static_cast<i64>(b.pos.x) - a.pos.x, static_cast<i64>(b.pos.z) - a.pos.z);
    TDir d0, d3;
    if (!a.into.isZero() && !b.into.isZero()) {
        d0 = a.into;
        d3 = -b.into;
    } else if (!a.into.isZero()) {
        d0 = a.into;
        d3 = reflect(d0, c);
    } else if (!b.into.isZero()) {
        d3 = -b.into;
        d0 = reflect(d3, c);
    } else {
        d0 = d3 = c;
    }
    if (options.straight) {
        // Within a degree of the line between the ends.
        constexpr i64 kCos1 = static_cast<i64>(kDirOne) * kDirOne * 99985 / 100000;
        if (dot(d0, c) < kCos1 || dot(d3, c) < kCos1) return fail("This must be straight");
        d0 = d3 = c;
    }
    // Neither end may point away from the other (that would need a loop).
    constexpr i64 kCos80 = static_cast<i64>(kDirOne) * kDirOne * 174 / 1000;
    if (dot(d0, c) < kCos80 || dot(d3, c) < kCos80) return fail("Too sharp a turn - add a curve in between");

    // Handle lengths: a third of the chord for straight track, a little more for curves (this
    // closely follows a circular arc for turns up to 90 degrees).
    const i64 cosTurn = dot(d0, d3) / kDirOne;
    const i64 f = std::min<i64>(kDirOne * 6 / 10, kDirOne / 3 + (kDirOne - cosTurn) * 233 / kDirOne);
    const i64 handle = chord * f / kDirOne;
    const Curve curve{{a.pos, offset(a.pos, d0, handle), offset(b.pos, -d3, handle), b.pos}};

    // Height profile. Ends joining existing track keep its height and gradient. A free end
    // follows the ground as far as a smooth vertical curve allows: with one end's gradient
    // fixed, the other end's gradient mirrors it about the average gradient, which makes the
    // profile a parabola whose gradient changes evenly and never exceeds either end's.
    std::vector<TrackSample> probe = sampleCurve(curve, 0, 0, 0, 0, sampleCount(curve));
    const i64 len = std::max<i64>(1, probe.back().s);
    constexpr i64 kMax = kTrackMaxGrade * kGradeOne / kTrackUnits;
    const auto heightFor = [&](i64 grade) { return grade * len / kGradeOne; }; // height change over the track
    i64 h0, h1, g0, g1;
    if (options.level) {
        // Level from end to end, at the height of the ground (or of the track it joins). A track
        // end on a gentle slope is levelled to meet it; a steeper one is refused.
        constexpr i64 kLevellable = 2 * kGradeOne / kTrackUnits; // 2 fine units per tile
        if ((a.existing && std::abs(a.grade) > kLevellable) || (b.existing && std::abs(b.grade) > kLevellable))
            return fail("This track end is on a slope - put the station on level ground and join the track to it");
        pl.levelStart = a.existing && a.grade != 0;
        pl.levelEnd = b.existing && b.grade != 0;
        if (a.existing && b.existing && a.h != b.h) return fail("Stations need level track");
        if (a.existing) h0 = a.h;
        else if (b.existing) h0 = b.h;
        else {
            i64 sum = 0;
            for (int i = 0; i <= 4; ++i) sum += terrainAt(w, at(curve, kT * i / 4));
            h0 = sum / 5;
        }
        h1 = h0;
        g0 = g1 = 0;
    } else if (a.existing && b.existing) {
        h0 = a.h;
        h1 = b.h;
        g0 = a.grade;
        g1 = -b.grade;
    } else if (a.existing) {
        h0 = a.h;
        g0 = a.grade;
        h1 = std::clamp<i64>(terrainAt(w, b.pos), h0 + heightFor(g0 - kMax) / 2, h0 + heightFor(g0 + kMax) / 2);
        h1 = std::max<i64>(h1, 0);
        g1 = 2 * (h1 - h0) * kGradeOne / len - g0;
    } else if (b.existing) {
        h1 = b.h;
        g1 = -b.grade;
        h0 = std::clamp<i64>(terrainAt(w, a.pos), h1 - heightFor(g1 + kMax) / 2, h1 - heightFor(g1 - kMax) / 2);
        h0 = std::max<i64>(h0, 0);
        g0 = 2 * (h1 - h0) * kGradeOne / len - g1;
    } else {
        h0 = terrainAt(w, a.pos);
        h1 = std::max<i64>(0, std::clamp<i64>(terrainAt(w, b.pos), h0 - heightFor(kMax), h0 + heightFor(kMax)));
        g0 = g1 = (h1 - h0) * kGradeOne / len;
    }

    // Split into pieces, with samples lining up with the joints.
    const int pieces = static_cast<int>((len + kTrackPiece - 1) / kTrackPiece);
    const int per = std::max(4, (sampleCount(curve) + pieces - 1) / pieces);
    pl.samples = sampleCurve(curve, static_cast<i32>(h0), g0, static_cast<i32>(h1), g1, per * pieces);

    // Where the track crosses a road on the level, free ends move up or down a little so that the
    // rails meet the road exactly (roads keep their ground at whole height steps).
    if (options.kind == TrackKind::Plain && !(a.existing && b.existing)) {
        for (const TrackSample& sm : pl.samples) {
            const TilePos t = tileAt(sm.p);
            if (!crossableRoad(w, t)) continue;
            const Nearest near =
                nearest(pl.samples, {t.x * kTrackUnits + kTrackUnits / 2, t.y * kTrackUnits + kTrackUnits / 2});
            if (near.d > kCrossingReach) continue;
            const i64 miss = static_cast<i64>(terrainAt(w, near.q)) - near.h;
            if (miss == 0 || std::abs(miss) > 24) break;
            const i64 total = std::max<i32>(1, pl.samples.back().s);
            const i64 u = pl.samples[static_cast<usize>(near.i)].s * kT / total;
            if (!a.existing && !b.existing) {
                h0 += miss;
                h1 += miss;
            } else {
                // The height at the crossing changes in proportion to the free end's height.
                const bool moveEnd = !b.existing;
                const auto heightAt = [&](i64 free) {
                    const i64 hs = moveEnd ? h0 : free, he = moveEnd ? free : h1;
                    const i64 gs = moveEnd ? g0 : 2 * (he - hs) * kGradeOne / total - g1;
                    const i64 ge = moveEnd ? 2 * (he - hs) * kGradeOne / total - g0 : g1;
                    return hermite(u, hs, gs * total / kGradeOne, he, ge * total / kGradeOne);
                };
                const i64 current = moveEnd ? h1 : h0;
                const i64 step = heightAt(current + 64) - heightAt(current);
                if (std::abs(step) < 8) break;
                const i64 moved = current + miss * 64 / step;
                if (std::abs(moved - current) > 48 || moved < 0) break;
                if (moveEnd) {
                    h1 = moved;
                    g1 = 2 * (h1 - h0) * kGradeOne / total - g0;
                } else {
                    h0 = moved;
                    g0 = 2 * (h1 - h0) * kGradeOne / total - g1;
                }
            }
            pl.samples = sampleCurve(curve, static_cast<i32>(h0), g0, static_cast<i32>(h1), g1, per * pieces);
            break;
        }
    }
    pl.length = pl.samples.back().s;

    // Gradient and curvature checks. Curvature is measured over half a tile either side, so
    // that rounding of the sample positions does not read as a kink.
    i64 worstGrade = 0; // fine units per 1024 units
    i64 minRadius = INT64_MAX;
    const usize n = pl.samples.size();
    const usize k = static_cast<usize>(std::max<i64>(1, kTrackUnits / 2 * static_cast<i64>(n - 1) / std::max<i64>(1, pl.length)));
    // Gradients over a tile, as heights are whole fine units.
    const usize kg = std::min(n - 1, 2 * k);
    for (usize i = 0; i + kg < n; ++i) {
        const TrackSample& p = pl.samples[i];
        const TrackSample& q = pl.samples[i + kg];
        const i64 ds = std::max<i64>(1, q.s - p.s);
        worstGrade = std::max<i64>(worstGrade, std::abs(static_cast<i64>(q.h) - p.h) * 1024 / ds);
    }
    for (usize i = k; i + k < n; ++i) {
        const TPoint o = pl.samples[i - k].p, p = pl.samples[i].p, q = pl.samples[i + k].p;
        const i64 ax = static_cast<i64>(p.x) - o.x, az = static_cast<i64>(p.z) - o.z;
        const i64 bx = static_cast<i64>(q.x) - p.x, bz = static_cast<i64>(q.z) - p.z;
        const i64 cross = std::abs(ax * bz - az * bx);
        const i64 la = distance(o, p), lb = distance(p, q);
        if (cross * 2048 > la * lb) // turning more than ~0.03 degrees
            minRadius = std::min(minRadius, (la + lb) * la * lb / (2 * cross));
    }
    pl.gradePermille = static_cast<i32>(worstGrade * 8000 / 1024);
    pl.minRadius = minRadius == INT64_MAX ? 0 : static_cast<i32>(std::min<i64>(minRadius, INT_MAX));
    if (worstGrade * kTrackUnits > kTrackMaxGrade * 1024 * 5 / 4)
        return fail("Too steep - trains can only climb gentle slopes; make the track longer");
    if (pl.minRadius && pl.minRadius < kTrackMinRadius) return fail("Curve too tight");

    // Bridges and tunnels. Where the rails run high above the ground (or above water, a road or
    // a railway) the track goes onto a bridge; deep under the ground it goes through a tunnel.
    std::vector<TrackStructure> kinds(n, TrackStructure::Ground);
    if (options.kind == TrackKind::Plain) {
        std::vector<u8> mustBridge(n, 0);
        std::vector<i32> cover(n, 0);
        for (usize i = 0; i < n; ++i) {
            const TrackSample& sm = pl.samples[i];
            const Tile* tile = w.map.tile(tileAt(sm.p));
            if (!tile) return fail("Area not explored");
            const bool water = tile->type == TileType::Water;
            const bool over = water || tile->type == TileType::Road || tile->type == TileType::Rail || tile->type == TileType::Track;
            const i32 clearance = sm.h - terrainAt(w, sm.p);
            mustBridge[i] = water;
            cover[i] = -clearance;
            if (clearance >= kBridgeClearance || (over && clearance >= kOverpassClearance)) kinds[i] = TrackStructure::Bridge;
            else if (-clearance >= kTunnelCover) kinds[i] = TrackStructure::Tunnel;
        }
        pl.spans = tidyStructures(pl.samples, kinds, mustBridge, cover);
        for (const TrackSpan& sp : pl.spans)
            (sp.kind == TrackStructure::Bridge ? pl.bridgeLength : pl.tunnelLength) += sp.s1 - sp.s0;
    }

    // What lies under the track: the bed needs clear ground (or a road to cross on the level);
    // bridges stand clear of buildings; tunnels pass under anything.
    Money trees = 0;
    for (const BedTile& bt : bedTileList(pl.samples, pl.spans, options.halfWidth)) {
        const TilePos t = bt.t;
        const Tile* tile = w.map.tile(t);
        if (!tile) return fail("Area not explored");
        if (bt.kind == TrackStructure::Tunnel) continue;
        if (tile->span) return fail("Bridge or tunnel in the way");
        if (bt.kind == TrackStructure::Bridge) {
            if (tile->type == TileType::Building || tile->type == TileType::Station || tile->type == TileType::Depot)
                return fail("Building in the way of the bridge");
            continue;
        }
        pl.tiles.push_back(t);
        switch (tile->type) {
        case TileType::Clear: trees += tile->trees * prices::kRemoveTree; break;
        case TileType::Track:
            if (tile->owner != company.value) return fail("Owned by another company");
            break;
        case TileType::Water:
            return fail(options.kind == TrackKind::Plain ? "Too low to bridge the water - raise the track" : "Can't build on water");
        case TileType::Road: {
            if (bt.near.d >= kGrazeReach) break; // the edge of the bed only brushes the road
            if (bt.near.d > kCrossingReach || options.kind != TrackKind::Plain) return fail("Road in the way");
            // A level crossing: square enough across a straight road, at its height.
            if (tile->rail) return fail("Railway in the way");
            const std::optional<bool> axis = crossableRoad(w, t);
            if (!axis) return fail("Level crossings need a straight road without junctions, level across its width");
            const bool ns = *axis;
            const Nearest crossing = nearest(pl.samples, {t.x * kTrackUnits + kTrackUnits / 2, t.y * kTrackUnits + kTrackUnits / 2});
            const TrackSample& sa = pl.samples[static_cast<usize>(bt.near.i)];
            const TrackSample& sb = pl.samples[static_cast<usize>(bt.near.i) + 1];
            const i64 dx = static_cast<i64>(sb.p.x) - sa.p.x, dz = static_cast<i64>(sb.p.z) - sa.p.z;
            const i64 len2 = std::max<i64>(1, dx * dx + dz * dz);
            const i64 along = ns ? dz : dx; // the track's heading along the road
            if (along * along * 2 > len2) return fail("Cross the road more squarely");
            if (std::abs(crossing.h - terrainAt(w, crossing.q)) > 3)
                return fail("The track must meet the road at its level, or pass high over it on a bridge");
            ++pl.levelCrossings;
            break;
        }
        case TileType::Rail: return fail("Railway in the way");
        case TileType::Building: return fail("Building in the way");
        case TileType::Station: return fail("Station in the way");
        case TileType::Depot: return fail("Depot in the way");
        case TileType::Footpath: return fail("Station footpath in the way");
        }
    }

    // Separate tracks may not cross or crowd each other unless one passes well over the other
    // on a bridge (or under it in a tunnel); near the nodes this track joins they converge.
    {
        std::unordered_set<u32> nearby;
        for (const BedTile& bt : bedTileList(pl.samples, {}, options.halfWidth))
            if (const auto it = w.trackTiles.find(tileKey(bt.t)); it != w.trackTiles.end())
                for (const TrackEdgeId e : it->second) nearby.insert(e.value);
        const TPoint ends[2] = {a.pos, b.pos};
        const TrackNodeId joined[2] = {a.existing ? a.id : TrackNodeId{}, b.existing ? b.id : TrackNodeId{}};
        for (const u32 id : nearby) {
            const TrackEdge& other = w.trackEdges[TrackEdgeId{id}];
            const i32 spacing = (halfWidthOf(other.kind) + options.halfWidth) / 2 + kTrackSpacing - kTrackBedHalfWidth;
            // Where this track joins existing track at a switch, it runs alongside the other
            // branch for a while as the two diverge.
            i64 exclusion[2] = {3 * kTrackUnits / 2, 3 * kTrackUnits / 2};
            for (int j = 0; j < 2; ++j)
                if (joined[j].valid() && w.trackNodes[joined[j]].count(0) > 0 && w.trackNodes[joined[j]].count(1) > 0)
                    exclusion[j] = 7 * kTrackUnits;
            for (usize i = 0; i < n; ++i) {
                const TrackSample& sm = pl.samples[i];
                if (distance(sm.p, ends[0]) < exclusion[0] || distance(sm.p, ends[1]) < exclusion[1]) continue;
                const Nearest o = nearest(other.samples, sm.p);
                if (o.d >= spacing) continue;
                const TrackStructure mine = kinds[i];
                const TrackStructure theirs = structureAt(other.spans, segmentMid(other.samples, o.i));
                const i32 dh = sm.h - o.h;
                const bool over = dh >= kOverpassClearance && (mine == TrackStructure::Bridge || theirs == TrackStructure::Tunnel);
                const bool under = -dh >= kOverpassClearance && (mine == TrackStructure::Tunnel || theirs == TrackStructure::Bridge);
                if (!over && !under) return fail("Another track is in the way");
            }
        }
    }

    // Earthworks: a level bed under the rails, then embankments or cuttings easing back into
    // the surrounding ground over a distance that grows with the height difference.
    const auto locked = [&](i32 cx, i32 cy) {
        for (const TilePos t : {TilePos{cx - 1, cy - 1}, TilePos{cx, cy - 1}, TilePos{cx - 1, cy}, TilePos{cx, cy}}) {
            const Tile* tile = w.map.tile(t);
            if (!tile) return true;
            if (tile->type != TileType::Clear && tile->type != TileType::Track) return true;
            // Ground under another track's bridge or over its tunnel stays as it is.
            if (tile->type == TileType::Clear && w.trackTiles.contains(tileKey(t))) return true;
        }
        return false;
    };
    const i32 kBed = options.halfWidth + 48;
    constexpr i32 kBallastDepth = 5; // fine units the ground sits below the rails (the ballast fills the gap)
    const i32 kReach = kBed + 5 * kTrackUnits;
    i64 volume = 0;
    const Field field = nearestField(pl.samples, kReach, 0);
    // Ground already under the bed of existing track keeps its height, so building next to (or
    // on from) a track never buries or undermines it.
    std::unordered_set<u64> bedCorners;
    {
        std::unordered_set<u32> nearby;
        for (i32 gy = field.gy0; gy < field.gy0 + field.h; ++gy)
            for (i32 gx = field.gx0; gx < field.gx0 + field.w; ++gx)
                if (const auto it = w.trackTiles.find(tileKey({gx, gy})); it != w.trackTiles.end())
                    for (const TrackEdgeId e : it->second) nearby.insert(e.value);
        for (const u32 id : nearby) {
            const TrackEdge& oe = w.trackEdges[TrackEdgeId{id}];
            const Field other = nearestField(oe.samples, kBed, 0);
            for (i32 gy = other.gy0; gy < other.gy0 + other.h; ++gy)
                for (i32 gx = other.gx0; gx < other.gx0 + other.w; ++gx)
                    if (const Nearest* cell = other.at(gx, gy);
                        cell->d <= kBed && structureAt(oe.spans, segmentMid(oe.samples, cell->i)) == TrackStructure::Ground)
                        bedCorners.insert(tileKey({gx, gy}));
        }
    }
    for (i32 cy = field.gy0; cy < field.gy0 + field.h; ++cy) {
        for (i32 cx = field.gx0; cx < field.gx0 + field.w; ++cx) {
            const Nearest near = *field.at(cx, cy);
            if (near.d > kReach) continue;
            // No earthworks under bridges or over tunnels.
            if (structureAt(pl.spans, segmentMid(pl.samples, near.i)) != TrackStructure::Ground) continue;
            if (!w.map.cornerEditable(cx, cy) || locked(cx, cy) || bedCorners.contains(tileKey({cx, cy}))) continue;
            const i32 orig = w.map.cornerFine(cx, cy);
            const i64 bed = std::max(0, near.h - kBallastDepth);
            i64 target;
            if (near.d <= kBed) {
                target = bed;
            } else {
                const i64 diff = orig - bed;
                const i64 blend = std::clamp<i64>(std::abs(diff) * 16, kTrackUnits, 5 * kTrackUnits - 64);
                if (near.d > kBed + blend) continue;
                const i64 t = (near.d - kBed) * 1024 / blend;      // 0 at the bed, 1024 at the far edge
                const i64 smooth = t * t * (3 * 1024 - 2 * t) / (1024 * 1024); // smoothstep
                target = bed + diff * smooth / 1024;
            }
            target = std::clamp<i64>(target, 0, kMaxSmoothHeight * kSubLevels);
            if (target == orig) continue;
            pl.terrain.push_back({cx, cy, static_cast<u16>(orig), static_cast<u16>(target)});
            volume += std::abs(target - orig);
        }
    }
    pl.earthworks = volume * prices::kEarthworkPerUnit;
    const i64 onGround = pl.length - pl.bridgeLength - pl.tunnelLength;
    pl.cost = (onGround * prices::kTrackPerTile + static_cast<i64>(pl.bridgeLength) * prices::kTrackBridgePerTile +
               static_cast<i64>(pl.tunnelLength) * prices::kTrackTunnelPerTile) /
                  kTrackUnits +
              pl.levelCrossings * prices::kLevelCrossing + trees + pl.earthworks;

    // Pieces for building.
    for (int j = 0; j < pieces; ++j) {
        const i64 ta = kT * j / pieces, tb = kT * (j + 1) / pieces;
        TrackPlan::Piece pc;
        pc.p0 = blossom(curve, ta, ta, ta);
        pc.p1 = blossom(curve, ta, ta, tb);
        pc.p2 = blossom(curve, ta, tb, tb);
        pc.p3 = blossom(curve, tb, tb, tb);
        if (j == 0) pc.p0 = a.pos;
        if (j == pieces - 1) pc.p3 = b.pos;
        pc.d0 = j == 0 ? d0 : normalised(static_cast<i64>(pc.p1.x) - pc.p0.x, static_cast<i64>(pc.p1.z) - pc.p0.z);
        pc.d3 = j == pieces - 1 ? d3 : normalised(static_cast<i64>(pc.p3.x) - pc.p2.x, static_cast<i64>(pc.p3.z) - pc.p2.z);
        const TrackSample& sa = pl.samples[static_cast<usize>(j * per)];
        const TrackSample& sb = pl.samples[static_cast<usize>((j + 1) * per)];
        pc.h0 = sa.h;
        pc.h1 = sb.h;
        pc.s0 = sa.s;
        pc.s1 = sb.s;
        const i64 t0 = g0 * len / kGradeOne, t1 = g1 * len / kGradeOne;
        const auto gradeAt = [&](const TrackSample& s) {
            return hermiteDeriv(s.s * kT / pl.length, h0, t0, h1, t1) * kGradeOne / pl.length;
        };
        pc.g0 = j == 0 ? g0 : gradeAt(sa);
        pc.g1 = j == pieces - 1 ? g1 : gradeAt(sb);
        pl.pieces.push_back(pc);
    }
    pl.startNode = a.existing ? a.id : TrackNodeId{};
    pl.startSide = a.side;
    pl.endNode = b.existing ? b.id : TrackNodeId{};
    pl.endSide = b.side;
    pl.ok = true;
    return pl;
}

std::vector<TrackEdgeId> build(World& w, CompanyId company, const TrackPlan& pl) {
    const TrackKind kind = pl.options.kind;
    std::vector<TrackEdgeId> edges;
    if (!pl.ok || pl.pieces.empty()) return edges;
    // Level the end of the track a platform joins: its edges ease out flat into the node.
    for (const auto& [level, nid] : {std::pair{pl.levelStart, pl.startNode}, std::pair{pl.levelEnd, pl.endNode}}) {
        if (!level || !w.trackNodes.contains(nid)) continue;
        w.trackNodes[nid].grade = 0;
        for (const TrackEdgeId e : w.trackNodes[nid].edges)
            if (e.valid()) {
                computeSamples(w, w.trackEdges[e]);
                w.trackEdges[e].serial = ++w.nextSerial; // redrawn
            }
    }
    const auto attach = [&](TrackNodeId nid, u8 side, TrackEdgeId e) {
        TrackNode& n = w.trackNodes[nid];
        const usize slot = n.edges[side * 2u].valid() ? side * 2u + 1 : side * 2u;
        n.edges[slot] = e;
    };
    const auto newNode = [&](TPoint p, TDir d, i32 h, i64 grade) {
        const TrackNodeId id = w.trackNodes.create();
        TrackNode& n = w.trackNodes[id];
        n.pos = p;
        n.dir = d;
        n.h = h;
        n.grade = grade;
        return id;
    };
    TrackNodeId prev = pl.startNode.valid() ? pl.startNode
                                            : newNode(pl.pieces.front().p0, pl.pieces.front().d0, pl.pieces.front().h0,
                                                      pl.pieces.front().g0);
    u8 prevSide = pl.startNode.valid() ? pl.startSide : 0;
    for (usize j = 0; j < pl.pieces.size(); ++j) {
        const TrackPlan::Piece& pc = pl.pieces[j];
        const bool last = j + 1 == pl.pieces.size();
        TrackNodeId next;
        u8 nextSide = 1;
        if (last && pl.endNode.valid()) {
            next = pl.endNode;
            nextSide = pl.endSide;
        } else {
            // New nodes point along the direction of travel: the edge arrives on side 1.
            next = newNode(pc.p3, pc.d3, pc.h1, pc.g1);
        }
        const TrackEdgeId id = w.trackEdges.create();
        {
            TrackEdge& e = w.trackEdges[id];
            e.a = prev;
            e.b = next;
            e.sideA = prevSide;
            e.sideB = nextSide;
            e.handleA = static_cast<i32>(distance(pc.p0, pc.p1));
            e.handleB = static_cast<i32>(distance(pc.p3, pc.p2));
            e.owner = company;
            e.kind = kind;
            e.serial = ++w.nextSerial;
        }
        attach(prev, prevSide, id);
        attach(next, nextSide, id);
        TrackEdge& e = w.trackEdges[id];
        computeSamples(w, e);
        // The plan's bridges and tunnels along this piece, in the edge's own distances.
        const i64 planned = std::max(1, pc.s1 - pc.s0);
        for (const TrackSpan& sp : pl.spans) {
            const i32 from = std::max(sp.s0, pc.s0), to = std::min(sp.s1, pc.s1);
            if (from >= to) continue;
            e.spans.push_back({static_cast<i32>((from - pc.s0) * e.length / planned),
                               static_cast<i32>((to - pc.s0) * e.length / planned), sp.kind});
        }
        computeCrossings(w, e);
        std::vector<TilePos> ground, all;
        edgeTiles(e, &ground, &all);
        for (const TilePos t : all) {
            w.trackTiles[tileKey(t)].push_back(id);
            w.map.touch(t);
        }
        for (const TilePos t : ground) {
            Tile* tile = w.map.tile(t);
            if (!tile) continue;
            if (tile->type == TileType::Clear) {
                tile->type = TileType::Track;
                tile->trees = 0;
                tile->extra = 0;
                tile->owner = company.value;
                if (tile->ground == Ground::Rock || tile->ground == Ground::Rough) tile->ground = Ground::Grass;
            }
            if (tile->type == TileType::Track && tile->extra < 255) ++tile->extra;
            w.map.touch(t);
        }
        edges.push_back(id);
        prev = next;
        prevSide = 0;
    }
    for (const FineChange& c : pl.terrain) {
        w.map.setCornerFine(c.cx, c.cy, c.to);
        w.map.touch({c.cx, c.cy});
    }
    ++w.trackRevision;
    return edges;
}

void remove(World& w, TrackEdgeId id) {
    TrackEdge* e = w.trackEdges.tryGet(id);
    if (!e) return;
    std::vector<TilePos> ground, tiles;
    edgeTiles(*e, &ground, &tiles);
    if (e->kind == TrackKind::Platform && w.stations.contains(e->station)) {
        const StationId sid = e->station;
        for (const TilePos t : tiles) {
            bool shared = false;
            const auto it = w.trackTiles.find(tileKey(t));
            if (it != w.trackTiles.end()) for (auto other : it->second) {
                const auto* edge = w.trackEdges.tryGet(other);
                if (other != id && edge && edge->kind == TrackKind::Platform && edge->station == sid) shared = true;
            }
            if (shared) continue;
            if (std::ranges::find(w.stations[sid].tiles, t) != w.stations[sid].tiles.end()) {
                stations::removeTile(w, sid, t);
                if (!w.stations.contains(sid)) break;
            }
        }
    }
    const StationId station = e->station;
    for (const TilePos t : tiles) {
        auto it = w.trackTiles.find(tileKey(t));
        if (it != w.trackTiles.end()) {
            std::erase(it->second, id);
            if (it->second.empty()) w.trackTiles.erase(it);
        }
        w.map.touch(t);
    }
    for (const TilePos t : ground) {
        Tile* tile = w.map.tile(t);
        if (!tile || tile->type != TileType::Track) continue;
        if (tile->extra > 0) --tile->extra;
        if (tile->extra == 0) {
            tile->type = TileType::Clear;
            tile->owner = kOwnerNone;
        }
        w.map.touch(t);
    }
    const TrackNodeId nodes[2] = {e->a, e->b};
    for (const TrackNodeId nid : nodes) {
        TrackNode* n = w.trackNodes.tryGet(nid);
        if (!n) continue;
        for (TrackEdgeId& slot : n->edges)
            if (slot == id) slot = {};
        // Keep slot 0 of each side filled first.
        for (u8 s = 0; s < 2; ++s)
            if (!n->edges[s * 2u].valid()) std::swap(n->edges[s * 2u], n->edges[s * 2u + 1]);
    }
    w.trackEdges.destroy(id);
    if (auto* s = w.stations.tryGet(station)) for (auto& entry : s->entrances)
        if (entry.platform == id && !entry.path.empty()) entry.platform = platformNear(w,station,entry.path.back(),2*kTrackUnits);
    for (const TrackNodeId nid : nodes)
        if (const TrackNode* n = w.trackNodes.tryGet(nid); n && n->empty()) w.trackNodes.destroy(nid);
    ++w.trackRevision;
}

} // namespace track

} // namespace tp

namespace tp::track {

TrackNodeId runEnd(const World& w, EdgeRun r) {
    const TrackEdge& e = w.trackEdges[r.edge];
    return r.forward ? e.b : e.a;
}

TrackNodeId runStart(const World& w, EdgeRun r) {
    const TrackEdge& e = w.trackEdges[r.edge];
    return r.forward ? e.a : e.b;
}

u8 runEndSide(const World& w, EdgeRun r) {
    const TrackEdge& e = w.trackEdges[r.edge];
    return r.forward ? e.sideB : e.sideA;
}

std::vector<EdgeRun> continuations(const World& w, EdgeRun r) {
    std::vector<EdgeRun> out;
    const TrackNodeId nid = runEnd(w, r);
    const TrackNode& n = w.trackNodes[nid];
    const u8 outSide = static_cast<u8>(1 - runEndSide(w, r));
    for (u8 slot = 0; slot < 2; ++slot) {
        const TrackEdgeId id = n.edges[outSide * 2u + slot];
        if (!id.valid()) continue;
        const TrackEdge& e = w.trackEdges[id];
        out.push_back({id, e.a == nid && e.sideA == outSide});
    }
    return out;
}

TrackSample pointOn(const TrackEdge& e, bool forward, i32 dist) {
    const std::vector<TrackSample>& sm = e.samples;
    if (sm.empty()) return {};
    const i32 s = std::clamp(forward ? dist : e.length - dist, 0, e.length);
    usize i = 0;
    usize lo = 0, hi = sm.size() - 1;
    while (lo + 1 < hi) {
        const usize mid = (lo + hi) / 2;
        if (sm[mid].s <= s) lo = mid;
        else hi = mid;
    }
    i = lo;
    const TrackSample& a = sm[i];
    const TrackSample& b = sm[std::min(i + 1, sm.size() - 1)];
    const i64 span = std::max(1, b.s - a.s);
    const i64 t = std::clamp<i64>(s - a.s, 0, span);
    return {TPoint{static_cast<i32>(a.p.x + (static_cast<i64>(b.p.x) - a.p.x) * t / span),
                   static_cast<i32>(a.p.z + (static_cast<i64>(b.p.z) - a.p.z) * t / span)},
            static_cast<i32>(a.h + (static_cast<i64>(b.h) - a.h) * t / span), s};
}

bool reservable(const World& w, TrackEdgeId id, VehicleId train) {
    const TrackEdge& e = w.trackEdges[id];
    if (e.reservedBy.valid() && e.reservedBy != train) return false;
    if (e.reservedBy != train)
        for (const TilePos t : e.crossings)
            if (crossingRoadOccupied(w, t)) return false;
    // The two branches of a switch overlap near the switch: only one train at a time.
    const auto siblingFree = [&](TrackNodeId nid, u8 side) {
        const TrackNode& n = w.trackNodes[nid];
        for (u8 slot = 0; slot < 2; ++slot) {
            const TrackEdgeId other = n.edges[side * 2u + slot];
            if (!other.valid() || other == id) continue;
            const VehicleId by = w.trackEdges[other].reservedBy;
            if (by.valid() && by != train) return false;
        }
        return true;
    };
    return siblingFree(e.a, e.sideA) && siblingFree(e.b, e.sideB);
}

std::vector<TrackEdgeId> platformChain(const World& w, TrackEdgeId start) {
    std::vector<TrackEdgeId> chain;
    const TrackEdge* first = w.trackEdges.tryGet(start);
    if (!first || first->kind != TrackKind::Platform) return chain;
    chain.push_back(start);
    for (usize i = 0; i < chain.size(); ++i) {
        const TrackEdge& e = w.trackEdges[chain[i]];
        // Straight on through each end node: the edges on that node's other side.
        for (const auto& [nid, side] : {std::pair{e.a, e.sideA}, std::pair{e.b, e.sideB}}) {
            const TrackNode& n = w.trackNodes[nid];
            for (u8 slot = 0; slot < 2; ++slot) {
                const TrackEdgeId other = n.edges[(1u - side) * 2u + slot];
                if (!other.valid() || std::ranges::find(chain, other) != chain.end()) continue;
                const TrackEdge& o = w.trackEdges[other];
                if (o.kind == TrackKind::Platform && o.station == first->station) chain.push_back(other);
            }
        }
    }
    std::ranges::sort(chain, [](TrackEdgeId x, TrackEdgeId y) { return x.value < y.value; });
    return chain;
}

std::vector<std::vector<TrackEdgeId>> platformsOf(const World& w, StationId s) {
    std::vector<std::vector<TrackEdgeId>> out;
    std::vector<TrackEdgeId> seen;
    for (const auto [id, e] : w.trackEdges) {
        if (e.kind != TrackKind::Platform || e.station != s || std::ranges::find(seen, id) != seen.end()) continue;
        std::vector<TrackEdgeId> chain = platformChain(w, id);
        seen.insert(seen.end(), chain.begin(), chain.end());
        out.push_back(std::move(chain));
    }
    const auto firstBuilt = [&](const std::vector<TrackEdgeId>& c) {
        u32 serial = UINT32_MAX;
        for (const TrackEdgeId e : c) serial = std::min(serial, w.trackEdges[e].serial);
        return serial;
    };
    std::ranges::sort(out, [&](const auto& x, const auto& y) { return firstBuilt(x) < firstBuilt(y); });
    return out;
}

TrackEdgeId platformNear(const World& w, StationId s, TPoint p, i32 radius) {
    TrackEdgeId best;
    i64 bestD = static_cast<i64>(radius) + 1;
    for (const auto [id, e] : w.trackEdges) {
        if (e.kind != TrackKind::Platform || e.station != s) continue;
        const i64 d = nearest(e.samples, p).d;
        if (d < bestD) {
            bestD = d;
            best = id;
        }
    }
    return best;
}

TPoint platformPoint(const World& w, const std::vector<TrackEdgeId>& chain) {
    if (chain.empty()) return {};
    const TrackEdge& e = w.trackEdges[chain.front()];
    return pointOn(e, true, e.length / 2).p;
}

int platformNumber(const World& w, StationId s, TPoint p) {
    const TrackEdgeId near = platformNear(w, s, p);
    if (!near.valid()) return 0;
    const auto platforms = platformsOf(w, s);
    for (usize i = 0; i < platforms.size(); ++i)
        if (std::ranges::find(platforms[i], near) != platforms[i].end()) return static_cast<int>(i) + 1;
    return 0;
}

bool edgeCovers(const World& w, TrackEdgeId e, TilePos t) {
    const auto it = w.trackTiles.find(tileKey(t));
    return it != w.trackTiles.end() && std::ranges::find(it->second, e) != it->second.end();
}

bool hasPlatform(const World& w, StationId s) {
    for (const auto [id, e] : w.trackEdges)
        if (e.kind == TrackKind::Platform && e.station == s) return true;
    return false;
}

TrackPlan planPlatform(const World& w, CompanyId company, const TrackAnchor& from, TPoint to) {
    TrackAnchor start = from;
    TDir dir{0, 0};
    if (start.node.valid()) {
        const TrackNode* n = w.trackNodes.tryGet(start.node);
        if (!n) {
            TrackPlan p;
            p.error = "That track no longer exists";
            return p;
        }
        // Continue straight out of the node's open side.
        const int side = n->count(0) == 0 ? 0 : (n->count(1) == 0 ? 1 : -1);
        if (side < 0) {
            TrackPlan p;
            p.error = "Start a station at the open end of a track";
            return p;
        }
        dir = n->side(static_cast<u8>(side));
        start.pos = n->pos;
        start.into = dir;
    } else {
        dir = normalised(static_cast<i64>(to.x) - start.pos.x, static_cast<i64>(to.z) - start.pos.z);
        start.into = dir;
    }
    // Snap the length to whole tiles along the platform's line.
    const i64 along = (static_cast<i64>(to.x) - start.pos.x) * dir.x / kDirOne +
                      (static_cast<i64>(to.z) - start.pos.z) * dir.z / kDirOne;
    const i64 tiles = std::clamp<i64>((along + kTrackUnits / 2) / kTrackUnits, 2, 16);
    TrackAnchor end;
    end.pos = offset(start.pos, dir, tiles * kTrackUnits);
    end.into = -dir;
    TrackOptions opt;
    opt.kind = TrackKind::Platform;
    opt.straight = true;
    opt.level = true;
    opt.halfWidth = kPlatformHalfWidth;
    TrackPlan p = plan(w, company, start, end, opt);
    if (p.ok) p.cost += tiles * prices::kRailStationTile;
    return p;
}

TrackPlan planParallelPlatform(const World& w, CompanyId company, const TrackPlan& first, int index) {
    if (first.samples.size() < 2) return first;
    const TPoint a = first.samples.front().p, b = first.samples.back().p;
    const TDir dir = normalised(static_cast<i64>(b.x) - a.x, static_cast<i64>(b.z) - a.z);
    const TDir normal{-dir.z, dir.x};
    TrackAnchor from;
    from.pos = offset(a, normal, index * 2 * kTrackUnits);
    return planPlatform(w, company, from, offset(from.pos, dir, first.length));
}

StationId buildPlatform(World& w, CompanyId company, const TrackPlan& pl, StationId join, StationUse use) {
    const std::vector<TrackEdgeId> edges = build(w, company, pl);
    if (edges.empty()) return {};
    // Join a station of the company with a platform close by, or found a new one.
    std::vector<TilePos> tiles;
    for (const TrackEdgeId e : edges)
        for (const TilePos t : bedTiles(w.trackEdges[e].samples, kPlatformHalfWidth)) tiles.push_back(t);
    std::ranges::sort(tiles);
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
    StationId sid = join;
    for (const auto [id, st] : w.stations) {
        if (sid.valid()) break;
        if (st.owner != company) continue;
        if (use != StationUse::Both && st.use != use && st.use != StationUse::Both) continue;
        for (const TilePos a : st.tiles) {
            for (const TilePos b : tiles)
                if (chebyshev(a.x - b.x, a.y - b.y) <= 2) {
                    sid = id;
                    break;
                }
            if (sid.valid()) break;
        }
        if (sid.valid()) break;
    }
    const TilePos middle = tiles[tiles.size() / 2];
    if (!sid.valid()) {
        sid = stations::create(w, company, middle, StopKind::Rail);
        w.stations[sid].use = use;
    } else {
        stations::addTile(w, sid, middle, StopKind::Rail);
    }
    for (const TilePos t : tiles)
        if (t != middle && std::ranges::find(w.stations[sid].tiles, t) == w.stations[sid].tiles.end())
            stations::addTile(w, sid, t, StopKind::Rail);
    for (const TrackEdgeId e : edges) w.trackEdges[e].station = sid;
    passengers::markDirty(w);
    return sid;
}

TrackPlan planDepot(const World& w, CompanyId company, TrackNodeId node) {
    TrackPlan p;
    const TrackNode* n = w.trackNodes.tryGet(node);
    if (!n) {
        p.error = "Point at the open end of a track";
        return p;
    }
    const int side = n->count(0) == 0 ? 0 : (n->count(1) == 0 ? 1 : -1);
    if (side < 0) {
        p.error = "Point at the open end of a track";
        return p;
    }
    const TDir dir = n->side(static_cast<u8>(side));
    TrackAnchor from;
    from.node = node;
    from.pos = n->pos;
    from.into = dir;
    TrackAnchor to;
    to.pos = offset(n->pos, dir, kDepotStub);
    to.into = -dir;
    TrackOptions opt;
    opt.kind = TrackKind::Depot;
    opt.straight = true;
    p = plan(w, company, from, to, opt);
    if (!p.ok) return p;
    // The building stands just beyond the stub.
    const TilePos site = tileAt(depotBuildingPos(p));
    const Tile* tile = w.map.tile(site);
    if (!tile) {
        p.ok = false;
        p.error = "Area not explored";
        return p;
    }
    if (tile->type != TileType::Clear && tile->type != TileType::Track) {
        p.ok = false;
        p.error = "No room for the depot building";
        return p;
    }
    if (tile->type == TileType::Track && std::ranges::find(p.tiles, site) == p.tiles.end()) {
        p.ok = false;
        p.error = "Another track is in the way";
        return p;
    }
    p.cost += prices::kRailDepot;
    return p;
}

TPoint depotBuildingPos(const TrackPlan& p) {
    const TrackPlan::Piece& last = p.pieces.back();
    return offset(last.p3, last.d3, kTrackUnits * 2 / 5);
}

TDir depotFacing(const TrackPlan& p) { return -p.pieces.back().d3; }

} // namespace tp::track
