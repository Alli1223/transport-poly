#include "sim/road.hpp"

#include "sim/map.hpp"

namespace tp {

namespace {

// Corner indices (NW, NE, SE, SW) of the edge facing each direction.
constexpr int kEdgeCorners[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};

bool tileAcceptsRoad(const Tile& t) { return t.type == TileType::Clear || t.type == TileType::Road; }

} // namespace

Surface roadSurface(const Slope& s, u8 pieces) {
    if (s.isFlat()) return Surface::Flat;
    if (s.isSteep()) return Surface::Invalid;
    if (const auto up = s.rampUphill()) {
        // Straight up / down the ramp is fine, anything else needs a foundation.
        if ((pieces & ~axisBits(*up)) == 0) return Surface::Ramp;
        return Surface::Foundation;
    }
    return Surface::Foundation;
}

Surface buildingSurface(const Slope& s) {
    if (s.isFlat()) return Surface::Flat;
    if (s.isSteep()) return Surface::Invalid;
    return Surface::Foundation;
}

int roadEdgeHalfLevels(const TileCorners& c, Surface surface, Dir d) {
    const Slope s = computeSlope(c);
    switch (surface) {
    case Surface::Flat: return 2 * s.base;
    case Surface::Foundation: return 2 * (s.base + 1);
    case Surface::Ramp: {
        const auto* e = kEdgeCorners[static_cast<int>(d)];
        return c.h[e[0]] + c.h[e[1]];
    }
    case Surface::Invalid: break;
    }
    return -1000;
}

u8 flatSurfaceLevel(const Slope& s, Surface surface) {
    return static_cast<u8>(surface == Surface::Foundation ? s.base + 1 : s.base);
}

bool canConnectRoad(const Map& map, TilePos a, Dir d) {
    const TilePos b = neighbour(a, d);
    const Tile* ta = map.tile(a);
    const Tile* tb = map.tile(b);
    if (!ta || !tb || !tileAcceptsRoad(*ta) || !tileAcceptsRoad(*tb)) return false;
    const Dir od = opposite(d);
    if (hasRoadPiece(*ta, d) && hasRoadPiece(*tb, od)) return false; // already connected
    const u8 pa = static_cast<u8>(roadPieces(*ta) | dirBit(d));
    const u8 pb = static_cast<u8>(roadPieces(*tb) | dirBit(od));
    const TileCorners ca = map.corners(a);
    const TileCorners cb = map.corners(b);
    const Surface sa = roadSurface(computeSlope(ca), pa);
    const Surface sb = roadSurface(computeSlope(cb), pb);
    if (sa == Surface::Invalid || sb == Surface::Invalid) return false;
    // Existing pieces must keep a valid connection when a tile switches between ramp and
    // foundation, so only allow the change when the tile had no other pieces or stays the same.
    if (roadPieces(*ta) && roadSurface(computeSlope(ca), roadPieces(*ta)) != sa) return false;
    if (roadPieces(*tb) && roadSurface(computeSlope(cb), roadPieces(*tb)) != sb) return false;
    return roadEdgeHalfLevels(ca, sa, d) == roadEdgeHalfLevels(cb, sb, od);
}

bool addRoadPiece(Map& map, TilePos t, Dir d, u8 owner) {
    Tile* tile = map.tile(t);
    if (!tile || !tileAcceptsRoad(*tile)) return false;
    const u8 pieces = static_cast<u8>(roadPieces(*tile) | dirBit(d));
    const Slope s = map.slope(t);
    const Surface surf = roadSurface(s, pieces);
    if (surf == Surface::Invalid) return false;
    if (roadPieces(*tile) && roadSurface(s, roadPieces(*tile)) != surf) return false;
    if (tile->type == TileType::Clear) {
        tile->type = TileType::Road;
        tile->trees = 0;
        tile->roadOwner = owner;
        if (tile->ground == Ground::Rock || tile->ground == Ground::Rough) tile->ground = Ground::Grass;
    }
    tile->road = static_cast<u8>((tile->road & ~kRoadPieceMask) | pieces);
    map.touch(t);
    return true;
}

bool connectRoad(Map& map, TilePos a, Dir d, u8 owner) {
    if (!canConnectRoad(map, a, d)) return false;
    addRoadPiece(map, a, d, owner);
    addRoadPiece(map, neighbour(a, d), opposite(d), owner);
    return true;
}

} // namespace tp
