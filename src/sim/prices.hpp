#pragma once
// Base prices for construction and services.

#include "core/money.hpp"

namespace tp::prices {

inline constexpr Money kTerraformPerCorner = 300;
inline constexpr Money kClearGrass = 20;
inline constexpr Money kClearRough = 60;
inline constexpr Money kClearRock = 150;
inline constexpr Money kRemoveTree = 50;
inline constexpr Money kPlantTree = 120;
inline constexpr Money kLoanStep = 10'000;

} // namespace tp::prices
