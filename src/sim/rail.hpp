#pragma once
// Railway track: six track pieces per tile (OpenTTD style), slopes, connectivity, signals and
// path reservations.
//
// Pieces connect two tile edges: the straight pieces NS and EW, and four corner pieces that each
// cut one corner of the tile (NE connects the north and east edges, and so on). Diagonal lines are
// chains of alternating corner pieces. Tile::rail holds the piece bits, Tile::extra2 the pieces
// currently reserved by trains and, on plain rail tiles, Tile::extra the signals.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"
#include "sim/road.hpp"
#include "sim/slope.hpp"
#include "sim/tile.hpp"

#include <vector>

namespace tp {

class Map;
class World;

enum TrackBit : u8 {
    kTrackNS = 1 << 0,
    kTrackEW = 1 << 1,
    kTrackNE = 1 << 2,
    kTrackSE = 1 << 3,
    kTrackSW = 1 << 4,
    kTrackNW = 1 << 5,
    kTrackAll = 0x3F,
};

struct TrackEdges {
    Dir a;
    Dir b;
};

constexpr TrackEdges trackEdges(u8 bit) {
    switch (bit) {
    case kTrackNS: return {Dir::N, Dir::S};
    case kTrackEW: return {Dir::E, Dir::W};
    case kTrackNE: return {Dir::N, Dir::E};
    case kTrackSE: return {Dir::S, Dir::E};
    case kTrackSW: return {Dir::S, Dir::W};
    case kTrackNW: return {Dir::N, Dir::W};
    default: return {Dir::N, Dir::S};
    }
}

// The piece connecting two different edges.
constexpr u8 trackBetween(Dir a, Dir b) {
    const u8 m = static_cast<u8>(dirBit(a) | dirBit(b));
    if (m == (dirBit(Dir::N) | dirBit(Dir::S))) return kTrackNS;
    if (m == (dirBit(Dir::E) | dirBit(Dir::W))) return kTrackEW;
    if (m == (dirBit(Dir::N) | dirBit(Dir::E))) return kTrackNE;
    if (m == (dirBit(Dir::S) | dirBit(Dir::E))) return kTrackSE;
    if (m == (dirBit(Dir::S) | dirBit(Dir::W))) return kTrackSW;
    if (m == (dirBit(Dir::N) | dirBit(Dir::W))) return kTrackNW;
    return 0;
}

constexpr bool isStraightTrack(u8 bit) { return bit == kTrackNS || bit == kTrackEW; }

constexpr Dir otherEdge(u8 bit, Dir e) {
    const TrackEdges t = trackEdges(bit);
    return t.a == e ? t.b : t.a;
}

// Pieces touching edge e.
constexpr u8 tracksAtEdge(Dir e) {
    switch (e) {
    case Dir::N: return kTrackNS | kTrackNE | kTrackNW;
    case Dir::E: return kTrackEW | kTrackNE | kTrackSE;
    case Dir::S: return kTrackNS | kTrackSE | kTrackSW;
    case Dir::W: return kTrackEW | kTrackSW | kTrackNW;
    }
    return 0;
}

// Two pieces on one tile conflict (cannot both be used at once) if they share an edge or cross.
constexpr bool tracksConflict(u8 a, u8 b) {
    if (a == b) return true;
    const TrackEdges ea = trackEdges(a), eb = trackEdges(b);
    if (ea.a == eb.a || ea.a == eb.b || ea.b == eb.a || ea.b == eb.b) return true;
    return (a | b) == (kTrackNS | kTrackEW);
}

// Path length of a piece in progress units (1 tile = 256).
constexpr i32 trackLength(u8 bit) { return isStraightTrack(bit) ? 256 : 181; }

// Rails may run straight up a ramp; otherwise non-steep slopes get a flat foundation.
Surface railSurface(const Slope& s, u8 bits);

// Signals on plain rail tiles with a single straight piece (Tile::extra):
// bit 0 = signal for trains travelling towards N (NS track) / E (EW track), bit 1 = the other way.
inline constexpr u8 kSignalForward = 1;
inline constexpr u8 kSignalBackward = 2;
inline constexpr u8 kSignalBlock = 4; // otherwise a path signal; direction bits remain in bits 0-1
// Signal bits facing a train travelling towards edge `travel` on a straight tile.
constexpr u8 signalFacing(u8 straightBit, Dir travel) {
    if (straightBit == kTrackNS) return travel == Dir::N ? kSignalForward : kSignalBackward;
    return travel == Dir::E ? kSignalForward : kSignalBackward;
}

// Track pieces usable by trains on any tile type (rail, rail station, rail depot), 0 otherwise.
u8 railPieces(const Tile& t);

// Whether `bit` may be added to the tile (terrain + contents, ignoring ownership).
bool canAddTrack(const Map& map, TilePos t, u8 bit);
// Whether trains can pass from tile `a` through its edge `d` into the neighbour.
bool railLinkOpen(const World& w, TilePos a, Dir d);
// Includes the rear of road vehicles that have only just left the crossing.
bool crossingRoadOccupied(const World& w, TilePos tile);
// Whole connected block, bounded by signals. Own reservations may be ignored when entering.
bool blockSignalClear(const World& w, TilePos signal, Dir travel, VehicleId ignore = {});

struct RailStep {
    TilePos tile;
    u8 bit;
};

// Track pieces of a rail line dragged from one tile to another: straight along the major axis,
// then diagonal (45 degrees) to reach the end tile.
std::vector<RailStep> planRailLine(TilePos from, TilePos to);

} // namespace tp
