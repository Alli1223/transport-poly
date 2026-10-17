#include "sim/rail.hpp"

#include "core/math.hpp"
#include "sim/map.hpp"
#include "sim/station.hpp"
#include "sim/world.hpp"

#include <cstdlib>

namespace tp {

Surface railSurface(const Slope& s, u8 bits) {
    if (s.isFlat()) return Surface::Flat;
    if (s.isSteep()) return Surface::Invalid;
    if (const auto up = s.rampUphill()) {
        const u8 straight = (*up == Dir::N || *up == Dir::S) ? kTrackNS : kTrackEW;
        if ((bits & ~straight) == 0) return Surface::Ramp;
        return Surface::Foundation;
    }
    return Surface::Foundation;
}

u8 railPieces(const Tile& t) {
    switch (t.type) {
    case TileType::Rail: return t.rail;
    case TileType::Station: return static_cast<StopKind>(t.extra) == StopKind::Rail ? t.rail : 0;
    case TileType::Depot: return t.rail; // the depot's straight piece; only its facing edge is open
    default: return 0;
    }
}

bool canAddTrack(const Map& map, TilePos t, u8 bit) {
    const Tile* tile = map.tile(t);
    if (!tile) return false;
    if (tile->type != TileType::Clear && tile->type != TileType::Rail) return false;
    if (tile->type == TileType::Rail && (tile->rail & bit)) return false;
    const u8 bits = static_cast<u8>((tile->type == TileType::Rail ? tile->rail : 0) | bit);
    const Slope s = map.slope(t);
    const Surface surf = railSurface(s, bits);
    if (surf == Surface::Invalid) return false;
    // Existing track must keep sitting the same way.
    if (tile->type == TileType::Rail && tile->rail && railSurface(s, tile->rail) != surf) return false;
    // Signals are only allowed on single straight tracks.
    if (tile->type == TileType::Rail && tile->extra != 0) return false;
    return true;
}

bool railLinkOpen(const World& w, TilePos a, Dir d) {
    const Tile* ta = w.map.tile(a);
    const TilePos b = neighbour(a, d);
    const Tile* tb = w.map.tile(b);
    if (!ta || !tb) return false;
    const u8 pa = railPieces(*ta);
    const u8 pb = railPieces(*tb);
    if (!(pa & tracksAtEdge(d)) || !(pb & tracksAtEdge(opposite(d)))) return false;
    // Depots connect only through their facing edge.
    if (ta->type == TileType::Depot && static_cast<Dir>(ta->extra) != d) return false;
    if (tb->type == TileType::Depot && static_cast<Dir>(tb->extra) != opposite(d)) return false;
    const auto surfaceOf = [&](TilePos t, const Tile& tile) {
        if (tile.type == TileType::Depot || tile.type == TileType::Station) return Surface::Flat;
        return railSurface(w.map.slope(t), tile.rail);
    };
    const TileCorners ca = w.map.corners(a), cb = w.map.corners(b);
    return roadEdgeHalfLevels(ca, surfaceOf(a, *ta), d) == roadEdgeHalfLevels(cb, surfaceOf(b, *tb), opposite(d));
}

std::vector<RailStep> planRailLine(TilePos from, TilePos to) {
    std::vector<RailStep> steps;
    if (from == to) return steps;
    const i32 dx = to.x - from.x, dy = to.y - from.y;
    const bool xMajor = std::abs(dx) >= std::abs(dy);
    // Heading of the straight part and of the diagonal part.
    const Dir major = xMajor ? (dx >= 0 ? Dir::E : Dir::W) : (dy >= 0 ? Dir::S : Dir::N);
    const Dir minor = xMajor ? (dy >= 0 ? Dir::S : Dir::N) : (dx >= 0 ? Dir::E : Dir::W);
    const i32 diag = xMajor ? std::abs(dy) : std::abs(dx);
    const i32 straight = (xMajor ? std::abs(dx) : std::abs(dy)) - diag;

    TilePos cur = from;
    Dir entry = opposite(major); // pretend we arrived from behind along the major axis
    const auto place = [&](Dir exit) {
        steps.push_back({cur, trackBetween(entry, exit)});
        cur = neighbour(cur, exit);
        entry = opposite(exit);
    };
    for (i32 i = 0; i < straight; ++i) place(major);
    // Diagonal: alternate turning towards the minor axis and back along the major axis.
    for (i32 i = 0; i < diag; ++i) {
        place(minor);
        place(major);
    }
    // Finish the last tile with a piece that continues the current heading.
    const Dir lastExit = entry == opposite(major) ? major : (entry == opposite(minor) ? minor : major);
    steps.push_back({cur, trackBetween(entry, lastExit == entry ? major : lastExit)});
    // Remove a possible duplicate overshoot (the walk may end one tile past `to` on diagonals).
    while (!steps.empty() && chebyshev(steps.back().tile.x - from.x, steps.back().tile.y - from.y) >
                                 chebyshev(to.x - from.x, to.y - from.y) + 1)
        steps.pop_back();
    return steps;
}

} // namespace tp
