#pragma once
// Base prices for construction and services.

#include "core/money.hpp"

namespace tp::prices {

inline constexpr Money kTerraformPerCorner = 300;
inline constexpr Money kClearGrass = 20;
inline constexpr Money kClearRough = 60;
inline constexpr Money kClearRock = 150;
inline constexpr Money kRemoveTree = 50;
inline constexpr Money kTrackPerTile = 450;     // free-form track, per tile of length
inline constexpr Money kCatenaryPerTile = 240;  // overhead wires over free-form track, per tile of length
inline constexpr Money kEarthworkPerUnit = 14;   // moving one corner by one fine height unit
inline constexpr Money kTrackBridgePerTile = 2200; // free-form track on a bridge, per tile of length
inline constexpr Money kTrackTunnelPerTile = 3000; // free-form track in a tunnel, per tile of length
inline constexpr Money kLevelCrossing = 900;
inline constexpr Money kPlantTree = 120;
inline constexpr Money kLoanStep = 10'000;
inline constexpr Money kRoadPiece = 250;
inline constexpr Money kRemoveRoadPiece = 40;
inline constexpr Money kRoadStop = 1'500;
inline constexpr Money kRemoveStation = 400;
inline constexpr Money kRoadDepot = 4'000;
inline constexpr Money kDock = 4'500;
inline constexpr Money kShipDepot = 7'500;
inline constexpr Money kRemoveDepot = 600;
inline constexpr Money kDemolishBuilding = 3'000;
inline constexpr Money kDemolishPerResident = 150;
inline constexpr Money kRailPiece = 400;
inline constexpr Money kRemoveRailPiece = 60;
inline constexpr Money kSignal = 500;
inline constexpr Money kRailStationTile = 2'500;
inline constexpr Money kRailDepot = 6'000;

// Monthly upkeep per owned piece / facility tile, in the same units as construction.
inline constexpr Money kRoadMaintenance = 2;
inline constexpr Money kTramMaintenance = 3;
inline constexpr Money kRailMaintenance = 4;
inline constexpr Money kSignalMaintenance = 5;
inline constexpr Money kStationMaintenance = 25;
inline constexpr Money kDepotMaintenance = 50;
inline constexpr Money kCanalTile = 1'200;
inline constexpr Money kReclaimWater = 2'000;
inline constexpr Money kCanalMaintenance = 8;

} // namespace tp::prices
