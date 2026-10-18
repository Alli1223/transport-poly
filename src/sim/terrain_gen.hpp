#pragma once
// Deterministic infinite terrain. Every value is a pure function of (seed, coordinate) computed
// with integer arithmetic only, so all peers generate identical chunks independently.
//
// The land field is a sum of value-noise octaves whose combined per-tile slope is provably
// below one height level; flooring it to integer levels therefore guarantees that adjacent
// corners never differ by more than one level - the OpenTTD terrain rule that keeps every tile
// a buildable slope type - without any neighbour-dependent fix-up across chunk borders.
// Rivers and lakes carve this field with slope-bounded sea-level basins.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/slope.hpp"
#include "sim/tile.hpp"

namespace tp {

struct Chunk;

inline constexpr i32 kMaxHeight = 64;
inline constexpr i32 kSnowLine = 34;
inline constexpr i32 kMaxSmoothHeight = 120; // levels
inline constexpr i32 kHeightOne = 1 << 16; // fixed point scale of raw heights

// The kind of land a world is made of.
enum class Landmass : u8 {
    Continents,   // large landmasses with oceans between them
    Mainland,     // land almost everywhere, with inland seas
    Islands,      // islands of every size in open sea
    Archipelago,  // many small islands and narrow straits
    SingleIsland, // one large island around the start, ocean beyond
};
inline constexpr int kLandmassCount = 5;

// The world's climate: which biomes its land is made of.
enum class Climate : u8 {
    Temperate, // meadows, woods and snowy peaks
    Arid,      // deserts with mesas, and dry steppe
    Arctic,    // tundra, taiga and glaciers
    Tropical,  // jungle, savanna and white beaches
    Varied,    // every biome, in broad regions across the world
};
inline constexpr int kClimateCount = 5;

enum class Biome : u8 { Temperate, Desert, Steppe, Tundra, Taiga, Jungle, Savanna };
inline constexpr int kTerrainAmountMax = 4; // mountains and lakes: none, few, normal, many, most
inline constexpr int kRiverAmountMax = 3;   // rivers: none, few, normal, many

// How a world's terrain is shaped. `style` 0 is the first smooth terrain (worlds saved before
// these settings existed); 1 is the current generator, which uses the other settings.
struct TerrainSettings {
    u8 style = 1;
    Landmass landmass = Landmass::Continents;
    u8 mountains = 2;
    u8 lakes = 2;
    u8 rivers = 2;
    Climate climate = Climate::Temperate; // saved from save version 20

    static TerrainSettings original() {
        TerrainSettings t;
        t.style = 0;
        return t;
    }
    friend bool operator==(const TerrainSettings&, const TerrainSettings&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(style, landmass, mountains, lakes, rivers);
        if (ar.version == 0 || ar.version >= 20) ar(climate);
        else if constexpr (Ar::kReading) climate = Climate::Temperate;
    }
};

class TerrainGenerator {
public:
    // `smooth` selects smooth terrain: continuous heights with varied inclines, shaped by
    // `terrain`. Without it the original stepped terrain (whole levels, neighbours at most one
    // level apart) is made, so that worlds saved before smooth terrain keep matching their
    // unexplored surroundings.
    explicit TerrainGenerator(u64 seed = 1, bool waterways = true, bool smooth = false,
                              TerrainSettings terrain = TerrainSettings::original());

    u64 seed() const { return seed_; }
    bool smooth() const { return smooth_; }
    const TerrainSettings& terrain() const { return terrain_; }

    // Unclamped height at a corner point in 1/65536 levels; negative below sea level.
    i64 rawHeight(i32 cx, i32 cy) const;
    // Corner height in fine units (kSubLevels per level).
    u16 cornerFine(i32 cx, i32 cy) const;
    // Corner height in whole levels.
    u8 cornerHeight(i32 cx, i32 cy) const { return levelOf(cornerFine(cx, cy)); }
    static u8 quantise(i64 raw);
    static u16 quantiseFine(i64 raw);

    // Fills heights and tiles (ground, water, trees) of a freshly activated chunk.
    void generateChunk(ChunkPos pos, Chunk& chunk) const;

    // Classifies a tile given its corner raw heights / integer heights (NW, NE, SE, SW).
    Tile generateTile(TilePos t, const i64 raw[4], const u16 fine[4]) const;

    // Forest density in [0, 65536) - also used by town placement to avoid dense forests.
    i64 forestDensity(i32 x, i32 y) const;

    // The biome at a point, and the kind of tree that grows there.
    Biome biomeAt(i32 x, i32 y) const;
    TreeType treeFor(TilePos t, i32 level) const;

private:
    i64 legacyHeight(i32 x, i32 y) const;
    i64 smoothHeight(i32 x, i32 y) const;
    i64 shapedHeight(i32 x, i32 y) const;
    i64 landNoise(i64 qx, i64 qy) const;
    // Biome strengths at a point (of kHeightOne): heat and moisture, and the share of desert.
    struct ClimateSample {
        i64 heat = 0, wet = 0, desert = 0;
        Biome biome = Biome::Temperate;
    };
    ClimateSample climateAt(i32 x, i32 y) const;
    i64 waterBed(i32 x, i32 y) const;

    u64 seed_;
    bool waterways_;
    bool smooth_;
    TerrainSettings terrain_;
    i64 startLift_ = 0; // raises the land around the start (current terrain only)
};

} // namespace tp
