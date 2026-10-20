#pragma once
// Shapes drawn over the map as overlay quads: rings, ribbons along polylines and legs that follow
// the ground. Shared by the person inspector and the lines layer.

#include "render/world_renderer.hpp"
#include "sim/world.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace tp::mapdraw {

glm::dvec3 groundPoint(const World& w, double x, double z, double lift);
// A ring lying flat around `centre`.
void addRing(std::vector<gfx::OverlayQuad>& overlays, glm::dvec3 centre, double radius, u32 colour);
// A ribbon along a polyline, dashed when `dash` > 0 (in tiles), shifted sideways by `offset`.
void addRibbon(std::vector<gfx::OverlayQuad>& overlays, const std::vector<glm::dvec3>& points, double halfWidth, u32 colour,
               double dash = 0.0, double offset = 0.0);
// A straight leg between two points, broken into pieces that follow the ground.
std::vector<glm::dvec3> groundLeg(const World& w, glm::dvec3 a, glm::dvec3 b, double lift);

} // namespace tp::mapdraw
