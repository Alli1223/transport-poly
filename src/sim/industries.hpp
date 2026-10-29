#pragma once
// Countryside industries: farms, forests, quarries and mines, and the plants that process their
// produce. Every 96-tile region of the world has one or two industry sites; the kind of industry
// suits the land there (logging camps in thick woods, quarries and mines on and around the
// mountains, farms on open lowland, fish farms in lakes and the sea near the shore). An industry
// is founded on its site once the land around it has been explored.

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
inline constexpr u16 kFishFarm = 29; // net pens out on the water
inline constexpr bool isCountryside(u16 buildingType) { return buildingType >= kGrainFarm && buildingType <= kFishFarm; }

// Industries keep their distance from towns: a new industry's footprint is at least this many
// tiles from every town house and town road, and this far beyond the built-up radius of every
// town's centre (and of the sites where towns are still to be founded, as big as they will be).
inline constexpr i32 kMinTownDistance = 12;

// Founds the industries whose sites became fully explored by activating `chunk`.
void onChunkActivated(World& w, ChunkPos chunk);
// Founds an industry as near `near` as the land allows (within `search` tiles): open, gently
// sloping ground, levelled for it (open water near the shore for a fish farm). Invalid when
// nowhere fits.
BuildingId found(World& w, u16 type, TilePos near, i32 search = 10);

} // namespace industries

} // namespace tp
