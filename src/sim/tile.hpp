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
};

enum class Ground : u8 { Grass, Rough, Rock, Sand, Snow, Water };

enum class TreeType : u8 { Deciduous, Conifer };

// Owner values stored in tiles (company ids are 0..kMaxCompanies-1).
inline constexpr u8 kOwnerNone = 0xFF;
inline constexpr u8 kOwnerTown = 0xFE;

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
    u32 index = 0; // building / station / depot id depending on type

    template <class Ar>
    void serialize(Ar& ar) {
        ar(type, ground, trees, treeType, road, rail, owner, roadOwner, extra, extra2, index);
    }
    friend bool operator==(const Tile&, const Tile&) = default;
};

static_assert(sizeof(Tile) <= 16, "keep tiles compact");

} // namespace tp
