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
inline constexpr Money kRoadPiece = 250;
inline constexpr Money kRemoveRoadPiece = 40;
inline constexpr Money kRoadStop = 1'500;
inline constexpr Money kRemoveStation = 400;
inline constexpr Money kRoadDepot = 4'000;
inline constexpr Money kRemoveDepot = 600;
inline constexpr Money kDemolishBuilding = 3'000;
inline constexpr Money kDemolishPerResident = 150;
inline constexpr Money kRailPiece = 400;
inline constexpr Money kRemoveRailPiece = 60;
inline constexpr Money kSignal = 500;
inline constexpr Money kRailStationTile = 2'500;
inline constexpr Money kRailDepot = 6'000;

} // namespace tp::prices
