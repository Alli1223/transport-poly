#pragma once
// Countryside industries: farms, forests, quarries and mines, and the plants that process their
// produce. Every 96-tile region of the world has one or two industry sites; the kind of industry
// suits the land there (logging camps in forests, mines in the mountains, farms on open
// lowland). An industry is founded on its site once the land around it has been explored.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

namespace tp {

class World;

namespace industries {

// Building types of the countryside industries.
inline constexpr u16 kGrainFarm = 15;
inline constexpr u16 kSheepFarm = 16;
inline constexpr u16 kDairyFarm = 17;
inline constexpr u16 kLoggingCamp = 18;
inline constexpr u16 kQuarry = 19;
inline constexpr u16 kIronMine = 20;
inline constexpr u16 kCoalMine = 21;
inline constexpr u16 kCheeseDairy = 22;
inline constexpr u16 kTextileMill = 23;
inline constexpr u16 kSawmill = 24;
inline constexpr u16 kBrickworks = 25;
inline constexpr u16 kSteelMill = 26;
inline constexpr u16 kGoodsFactory = 27;
inline constexpr u16 kFurnitureFactory = 28;
inline constexpr bool isCountryside(u16 buildingType) { return buildingType >= kGrainFarm && buildingType <= kFurnitureFactory; }

// Founds the industries whose sites became fully explored by activating `chunk`.
void onChunkActivated(World& w, ChunkPos chunk);
// Founds an industry as near `near` as the land allows (within `search` tiles): open, gently
// sloping ground, levelled for it. Invalid when nowhere fits.
BuildingId found(World& w, u16 type, TilePos near, i32 search = 10);

} // namespace industries

} // namespace tp
