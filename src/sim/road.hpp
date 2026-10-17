#pragma once
// Road pieces on tiles: terrain rules, connectivity and construction helpers.
//
// A road tile stores up to four half-tile pieces (N, E, S, W) running from the tile centre to an
// edge. Roads may run straight up a ramp slope; on other non-steep slopes they sit on a flat
// foundation one level above the lowest corner.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/slope.hpp"
#include "sim/tile.hpp"

namespace tp {

class Map;

inline constexpr u8 kRoadPieceMask = 0x0F;

constexpr u8 roadPieces(const Tile& t) { return t.road & kRoadPieceMask; }
constexpr u8 tramPieces(const Tile& t) { return static_cast<u8>(t.road >> 4); }
constexpr bool hasRoadPiece(const Tile& t, Dir d) { return (t.road & dirBit(d)) != 0; }
constexpr u8 axisBits(Dir d) { return static_cast<u8>(dirBit(d) | dirBit(opposite(d))); }

enum class Surface : u8 { Flat, Ramp, Foundation, Invalid };

// How a structure with the given road pieces sits on a slope.
Surface roadSurface(const Slope& s, u8 pieces);
// How a building sits on a slope (flat or foundation; steep slopes are invalid).
Surface buildingSurface(const Slope& s);

// Height, in half levels, of the road surface at the midpoint of the tile edge facing `d`.
int roadEdgeHalfLevels(const TileCorners& c, Surface surface, Dir d);
// Height in levels of a flat surface (Flat / Foundation).
u8 flatSurfaceLevel(const Slope& s, Surface surface);

// Whether pieces can be added on both sides of the edge between `a` and its neighbour in `d`
// (terrain, contents and edge heights).
bool canConnectRoad(const Map& map, TilePos a, Dir d);
// Adds the pieces; returns false (and changes nothing) if impossible. New road tiles get `owner`.
bool connectRoad(Map& map, TilePos a, Dir d, u8 owner);
// Adds a single dead-end piece without touching the neighbour (e.g. stubs towards buildings).
bool addRoadPiece(Map& map, TilePos t, Dir d, u8 owner);

} // namespace tp
