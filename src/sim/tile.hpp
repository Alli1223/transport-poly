#pragma once
// Per-tile game data stored in active chunks.

#include "core/types.hpp"

namespace tp {

enum class TileType : u8 {
    Clear,    // bare ground, optionally with trees
    Water,    // sea / lake at sea level
    Road,     // road and/or tram pieces (possibly with a level crossing)
    Rail,     // railway track pieces
    Building, // town building (house, office, shop, ...)
    Station,  // stop / platform / dock tile belonging to a station
    Depot,    // vehicle depot
    Track = 7,    // under free-form track (extra: number of track edges crossing the tile)
    Footpath = 8, // station entrance and approach pavement (index = station)
};

enum class Ground : u8 {
    Grass, Rough, Rock, Sand, Snow, Water,
    Desert, // dunes and dry sand
    Steppe, // dry grassland and savanna
    Tundra, // cold moss and scrub
    Jungle, // lush tropical undergrowth
};

enum class TreeType : u8 {
    Deciduous, Conifer, Palm, Cactus, Acacia, Shrub,
    Birch, Pine, Willow, Bush, // from terrain style 2
};

// Owner values stored in tiles (company ids are 0..kMaxCompanies-1).
inline constexpr u8 kOwnerNone = 0xFF;
inline constexpr u8 kOwnerTown = 0xFE;
inline constexpr u8 kSpanBridge = 1;
inline constexpr u8 kSpanTunnel = 2;
inline constexpr u8 kSpanAxisX = 4;
inline constexpr u8 kSpanCanal = 8; // restore owned canal water when dismantling a bridge
inline constexpr u8 kFreeTrackDepot = 0x80; // Tile::extra2 of a depot on free-form track

struct Tile {
    TileType type = TileType::Clear;
    Ground ground = Ground::Grass;
    u8 trees = 0; // number of trees on a clear tile (0-4)
    TreeType treeType = TreeType::Deciduous;
    u8 road = 0;               // bits 0-3: road pieces N/E/S/W, bits 4-7: tram pieces N/E/S/W
    u8 rail = 0;               // railway track bits (see infrastructure)
    u8 owner = kOwnerNone;     // owner of the rail / station / depot / tram tracks
    u8 roadOwner = kOwnerNone; // owner of the road pieces
    u8 extra = 0;              // type specific data (signals, depot direction, stop kind, ...)
    u8 extra2 = 0;
    u8 span = 0; // kind in low two bits, axis in bit 2; index packs length / tile offset
    u8 deckHeight = 0;
    u32 index = 0; // building / station / depot id depending on type

    template <class Ar>
    void serialize(Ar& ar) {
        ar(type, ground, trees, treeType, road, rail, owner, roadOwner, extra, extra2, index);
        if (ar.version == 0 || ar.version >= 7) ar(span, deckHeight);
    }
    friend bool operator==(const Tile&, const Tile&) = default;
};

constexpr bool spanPortal(const Tile& t) {
    return t.span && ((t.index & 0xFFFF) == 0 || (t.index & 0xFFFF) + 1 == (t.index >> 16));
}
constexpr bool waterUnderBridge(const Tile& t) { return (t.span & 3) == kSpanBridge && t.ground == Ground::Water; }
// A building standing in the water (a fish farm's pens): the water stays around it.
constexpr bool waterUnderBuilding(const Tile& t) { return t.type == TileType::Building && t.ground == Ground::Water; }

static_assert(sizeof(Tile) <= 16, "keep tiles compact");

} // namespace tp
