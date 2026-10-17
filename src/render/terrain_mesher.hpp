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

struct ChunkGeometry {
    std::vector<Vertex> ground;
    std::vector<Vertex> water;
    std::vector<TreePlacement> trees;
    float minY = 0.0f;
    float maxY = 0.0f;
};

// True when the tile at the given (world) position is water. Used for tiles outside the chunk.
using WaterLookup = std::function<bool(TilePos)>;

// lod 0 = one quad per tile; lod n merges 2^n x 2^n tiles per quad (no trees) for distant chunks.
// `world` (optional) provides building information for ground colours under buildings.
ChunkGeometry buildChunkGeometry(const Chunk& chunk, const TerrainGenerator& gen, const WaterLookup& waterOutside,
                                 int lod = 0, const World* world = nullptr);

// Triangulation shared by the mesher and picking: true if the tile is split along NE-SW.
bool tileSplitNESW(const TileCorners& c);
// Height (in levels) of the tile surface at fractional position (fx, fy) in [0,1].
float tileSurfaceLevel(const TileCorners& c, float fx, float fy);

} // namespace tp::gfx
