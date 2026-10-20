#pragma once
// Overhead wires over electrified free-form track: masts beside the line every so often, each with
// an arm over the track, and the contact wire and the catenary above it strung between them.

#include "render/models.hpp"
#include "sim/world.hpp"

#include <glm/glm.hpp>

namespace tp::gfx {

// Heights above the rails (tiles): the contact wire the pantographs press against, and the
// catenary wire it hangs from.
inline constexpr float kContactWireHeight = 0.47f;
inline constexpr float kCatenaryWireHeight = 0.54f;
inline constexpr float kMastOffset = 0.36f; // beside the track centre line, on its right

void addCatenary(InstanceBatch& batch, const World& w, const glm::dvec3& origin, const glm::dvec3& focus, double radius);

} // namespace tp::gfx
