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

bool canConnectRoad(const Map& map, TilePos a, Dir d, bool tram) {
    const TilePos b = neighbour(a, d);
    const Tile* ta = map.tile(a);
    const Tile* tb = map.tile(b);
    if (!ta || !tb || !tileAcceptsRoad(*ta) || !tileAcceptsRoad(*tb)) return false;
    const Dir od = opposite(d);
    if ((layerPieces(*ta, tram) & dirBit(d)) && (layerPieces(*tb, tram) & dirBit(od))) return false; // connected
    const u8 bit = tram ? static_cast<u8>(dirBit(d) << 4) : dirBit(d);
    const u8 obit = tram ? static_cast<u8>(dirBit(od) << 4) : dirBit(od);
    const u8 pa = static_cast<u8>(allRoadPieces(*ta) | ((ta->road | bit) & 0x0F) | (((ta->road | bit) >> 4) & 0x0F));
    const u8 pb = static_cast<u8>(allRoadPieces(*tb) | ((tb->road | obit) & 0x0F) | (((tb->road | obit) >> 4) & 0x0F));
    const TileCorners ca = map.corners(a);
    const TileCorners cb = map.corners(b);
    const Slope sla = computeSlope(ca);
    const Slope slb = computeSlope(cb);
    const Surface sa = roadSurface(sla, pa);
    const Surface sb = roadSurface(slb, pb);
    if (sa == Surface::Invalid || sb == Surface::Invalid) return false;
    // Existing pieces must not change how the tile sits (ramp vs foundation).
    if (allRoadPieces(*ta) && roadSurface(sla, allRoadPieces(*ta)) != sa) return false;
    if (allRoadPieces(*tb) && roadSurface(slb, allRoadPieces(*tb)) != sb) return false;
    return roadEdgeHalfLevels(ca, sa, d) == roadEdgeHalfLevels(cb, sb, od);
}

bool addRoadPiece(Map& map, TilePos t, Dir d, u8 owner, bool tram) {
    Tile* tile = map.tile(t);
    if (!tile || !tileAcceptsRoad(*tile)) return false;
    const u8 bit = tram ? static_cast<u8>(dirBit(d) << 4) : dirBit(d);
    const u8 newRoad = static_cast<u8>(tile->road | bit);
    const u8 all = static_cast<u8>((newRoad & 0x0F) | (newRoad >> 4));
    const Slope s = map.slope(t);
    const Surface surf = roadSurface(s, all);
    if (surf == Surface::Invalid) return false;
    if (allRoadPieces(*tile) && roadSurface(s, allRoadPieces(*tile)) != surf) return false;
    if (tile->type == TileType::Clear) {
        tile->type = TileType::Road;
        tile->trees = 0;
        tile->roadOwner = kOwnerNone;
        tile->owner = kOwnerNone;
        if (tile->ground == Ground::Rock || tile->ground == Ground::Rough) tile->ground = Ground::Grass;
    }
    if (tram) {
        if (tramPieces(*tile) == 0) tile->owner = owner;
    } else if (roadPieces(*tile) == 0) {
        tile->roadOwner = owner;
    }
    tile->road = newRoad;
    map.touch(t);
    return true;
}

bool connectRoad(Map& map, TilePos a, Dir d, u8 owner, bool tram) {
    if (!canConnectRoad(map, a, d, tram)) return false;
    addRoadPiece(map, a, d, owner, tram);
    addRoadPiece(map, neighbour(a, d), opposite(d), owner, tram);
    return true;
}

} // namespace tp
