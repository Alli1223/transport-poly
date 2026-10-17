#pragma once
// Mouse picking against the terrain height field.

#include "render/camera.hpp"
#include "sim/coords.hpp"
#include "sim/map.hpp"

#include <glm/glm.hpp>

namespace tp::gfx {

struct TerrainHit {
    bool hit = false;
    TilePos tile;
    glm::dvec3 point{0.0}; // world position of the hit
    double fx = 0.0;       // position inside the tile, [0,1)
    double fy = 0.0;
    TilePos corner; // nearest corner point (for terraforming)
    Dir edge = Dir::N; // nearest tile edge
};

TerrainHit pickTerrain(const Map& map, const Ray& ray, int maxTiles = 6000);

// World height (y) of the terrain surface at a world xz position.
double terrainHeightAt(const Map& map, double x, double z);

} // namespace tp::gfx
