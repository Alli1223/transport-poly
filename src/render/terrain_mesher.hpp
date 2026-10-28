#pragma once
// Builds the low poly geometry of a chunk: ground triangles, road surfaces, foundations, water
// surface and tree placements.

#include "core/types.hpp"
#include "render/mesh.hpp"
#include "sim/chunk.hpp"
#include "sim/terrain_gen.hpp"

#include <functional>
#include <vector>

namespace tp {
class World;
}

namespace tp::gfx {

// World units per terrain height level and the visual water surface height.
inline constexpr float kHeightStep = 0.5f;
inline constexpr float kWaterLevel = -0.12f;

struct TreePlacement {
    glm::vec3 pos; // chunk local
    float scale;
    float yaw;
    TreeType type;
    u8 variant;
    bool snowy;
};

// Grass, flowers and other small plants and stones on open ground, drawn near the camera.
enum class Cover : u8 { Grass, Flowers, Fern, Reeds, Heather, Pebbles };
struct CoverPlacement {
    glm::vec3 pos; // chunk local
    float yaw;
    float scale;
    Cover kind;
    u8 variant;
    u32 tint; // the ground's colour where it grows, for its blades
};

struct ChunkGeometry {
    std::vector<Vertex> ground;
    std::vector<Vertex> water;
    std::vector<TreePlacement> trees;
    std::vector<CoverPlacement> cover;
    float minY = 0.0f;
    float maxY = 0.0f;
};

// True when the tile at the given (world) position is water. Used for tiles outside the chunk.
using WaterLookup = std::function<bool(TilePos)>;
// The explored tile at a (world) position outside the chunk, or nullptr (the generator's tile is
// used there). Neighbouring tiles blend their ground colours across the chunk's border.
using TileLookup = std::function<const Tile*(TilePos)>;

// lod 0 = one quad per tile; lod n merges 2^n x 2^n tiles per quad (no trees) for distant chunks.
// `world` (optional) provides building information for ground colours under buildings, and the
// tiles around the chunk unless `tilesOutside` does. `cover` (lod 0 only) scatters ground cover
// on open ground: 1 as much as the land grows, less for fewer plants, 0 for none.
ChunkGeometry buildChunkGeometry(const Chunk& chunk, const TerrainGenerator& gen, const WaterLookup& waterOutside,
                                 int lod = 0, const World* world = nullptr, const std::function<bool(u32)>& residential = {},
                                 const TileLookup& tilesOutside = {}, float cover = 0.0f);

// Triangulation shared by the mesher and picking: true if the tile is split along NE-SW.
bool tileSplitNESW(const TileCorners& c);
bool tileSplitNESW(const FineCorners& c);
// Height (in levels) of the tile surface at fractional position (fx, fy) in [0,1].
float tileSurfaceLevel(const TileCorners& c, float fx, float fy);
float tileSurfaceLevel(const FineCorners& c, float fx, float fy);

} // namespace tp::gfx
