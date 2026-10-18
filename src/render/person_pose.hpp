#pragma once
#include "sim/person.hpp"
#include <glm/glm.hpp>
#include <optional>

namespace tp::gfx {
struct PersonPose { glm::dvec3 pos; float yaw = 0; bool visible = false; };
std::optional<PersonPose> personPose(const World& world, const Person& person, double tickFraction = 0);
}
