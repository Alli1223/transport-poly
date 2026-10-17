#pragma once
// Road network queries and A* path finding for road vehicles and trams.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"
#include "sim/vehicle.hpp"

#include <optional>
#include <vector>

namespace tp {

class World;

// Pieces a vehicle of `type` may drive on at `t` (0 if none). Depot tiles only count when they
// are `allowDepot` (vehicles never route through foreign depots).
u8 drivablePieces(const World& w, TilePos t, VehicleType type, DepotId allowDepot);

// True if a vehicle leaving `a` through edge `d` can enter the neighbouring tile.
bool roadLinkOpen(const World& w, TilePos a, Dir d, VehicleType type, DepotId allowDepot);

// Finds the sequence of exits to take starting at tile `start` (entered through `entry`) until a
// goal tile is reached. Deterministic. Returns nullopt when no route exists.
std::optional<std::vector<u8>> findRoadPath(const World& w, TilePos start, Dir entry,
                                            const std::vector<TilePos>& goals, VehicleType type,
                                            DepotId allowDepot, int maxNodes = 40000);

} // namespace tp
