#pragma once
// Internal: one handler overload per command payload. Handlers must not modify the world when
// `exec` is false and must return the same cost in both modes.

#include "sim/commands.hpp"

#include <unordered_set>

namespace tp::cmd {

CommandResult handle(World& w, const Command& c, const CmdActivateChunks& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdCreateCompany& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdSetLoan& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdTerraform& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdTerraformBrush& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdClearArea& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdPlantTrees& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildRoad& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdRemoveRoad& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildRoadStop& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildRoadDepot& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuyVehicle& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdSellVehicle& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdStartStopVehicle& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdSendToDepot& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdSetOrders& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdSkipOrder& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildRail& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdRemoveRail& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdCycleSignal& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildRailStation& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildRailDepot& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdAddCarriage& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdRemoveCarriage& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildDock& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildShipDepot& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdCloneVehicle& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdUnshareOrders& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdEditWater& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdBuildSpan& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdRemoveSpan& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdUpdateCompany& x, bool exec);

// Tiles currently occupied by vehicles, computed directly from the vehicle pool so the result
// never depends on derived caches (which differ right after loading a game).
std::unordered_set<u64> occupiedTiles(const World& w);
// Bulldozes a single tile for `company` (used by CmdClearArea). Returns cost or an error.
CommandResult demolishTile(World& w, CompanyId company, TilePos t, bool exec, const std::unordered_set<u64>& busy);

// Helpers shared by handlers.
struct Rect {
    i32 x0, y0, x1, y1; // inclusive
    i64 area() const { return static_cast<i64>(x1 - x0 + 1) * (y1 - y0 + 1); }
};
Rect normalise(TilePos a, TilePos b);

} // namespace tp::cmd
