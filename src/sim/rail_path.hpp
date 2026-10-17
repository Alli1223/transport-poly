#pragma once
// A* path finding over railway track (nodes are tile + entry edge).

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

#include <optional>
#include <vector>

namespace tp {

class World;

// Exits to take, starting at `start` (entered through `entry`), until a goal tile is entered.
// The returned exits do not include the goal tile itself. Reserved track is avoided when an
// alternative exists. Deterministic.
std::optional<std::vector<u8>> findRailPath(const World& w, TilePos start, Dir entry, const std::vector<TilePos>& goals,
                                            DepotId allowDepot, int maxNodes = 60000);

// Possible exits for a train entering tile `t` through edge `entry` (bit mask of Dir).
u8 railExits(const World& w, TilePos t, Dir entry, DepotId allowDepot);

} // namespace tp
