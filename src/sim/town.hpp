#pragma once
// Towns: deterministic sites in the infinite world, layout and growth state.

#include "core/rng.hpp"
#include "core/types.hpp"
#include "sim/buildings.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace tp {

class TerrainGenerator;
class World;

// The world is divided into town cells; each cell may contain one town site.
inline constexpr i32 kTownCellSize = 96;
// Tiles around a site centre that must be explored before the town is founded.
inline constexpr i32 kTownFootprint = 32;

struct TownSite {
    TilePos centre;
    TownClass cls = TownClass::Village;
    u32 seed = 0;
    u64 cellKey = 0;
    i32 targetPopulation = 0;
};

std::optional<TownSite> townSiteForCell(const TerrainGenerator& gen, i32 cellX, i32 cellY);

struct TripStats {
    i32 trips = 0;
    i32 walked = 0;
    i32 drove = 0;
    i32 lifts = 0; // no car, no transit: someone had to give them a lift (unserved demand)
    i32 transit = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(trips, walked, drove, lifts, transit);
    }
};

struct Town {
    std::string name;
    TilePos centre;
    u64 cellKey = 0;
    TownClass cls = TownClass::Village;
    bool grid = false;
    u8 gridSpacing = 3;
    Rng rng;
    i64 foundedTick = 0;

    // Cached aggregates (recomputed by recomputeTown).
    i32 population = 0;
    i32 jobs = 0;
    i32 workers = 0;
    i32 retailCapacity = 0;
    i32 radius = 6;

    i32 growthPoints = 0;
    std::vector<BuildingId> buildings;
    std::vector<BuildingId> jobSites;  // buildings offering jobs
    std::vector<BuildingId> retail;    // shopping destinations
    std::vector<TilePos> roads;        // tiles with town road pieces
    std::vector<PersonId> unemployed;  // may contain stale entries, validated on use
    TownId shopTown;                   // where residents go shopping (cached)
    u32 jobCursor = 0;

    TripStats thisMonth;
    TripStats lastMonth;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(name, centre, cellKey, cls, grid, gridSpacing, rng, foundedTick, population, jobs, workers,
           retailCapacity, radius, growthPoints, buildings, jobSites, retail, roads, unemployed, shopTown, jobCursor,
           thisMonth, lastMonth);
    }
};

std::string_view townClassName(TownClass c);

namespace towns {

// Founds every town whose footprint became fully explored by activating `chunk`.
void onChunkActivated(World& w, ChunkPos chunk);
TownId foundTown(World& w, const TownSite& site);
// Daily growth of all towns.
void growAll(World& w);
// Monthly bookkeeping (stats rollover, class upgrades, shop targets).
void monthly(World& w);
void recompute(World& w, TownId id);

BuildingId placeBuilding(World& w, TownId town, u16 type, TilePos pos, Dir facing, u8 variant);
void removeBuilding(World& w, BuildingId id);

// True if a building of the given size could be placed with its NW corner at `pos`.
bool canPlaceBuilding(const World& w, TilePos pos, u8 size);

} // namespace towns

} // namespace tp
