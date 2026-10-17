#pragma once
// World-space pose of vehicles for rendering and picking, interpolated between simulation ticks.

#include "sim/world.hpp"

#include <glm/glm.hpp>

#include <optional>

namespace tp::gfx {

struct VehiclePose {
    glm::dvec3 pos{0.0}; // world position (x, height, z)
    float yaw = 0.0f;    // heading around +Y; 0 = facing +X
    float pitch = 0.0f;  // nose up positive
};

// Lane offset from the road centre line (right hand traffic), in tiles.
inline constexpr float kLaneOffset = 0.2f;

// nullopt for vehicles that are not visible (inside a depot).
std::optional<VehiclePose> vehiclePose(const World& w, const Vehicle& v, double tickFraction);

// Height (world units) of the road / stop / depot surface at a point inside a tile.
float roadSurfaceY(const World& w, TilePos tile, float u, float v);

} // namespace tp::gfx
