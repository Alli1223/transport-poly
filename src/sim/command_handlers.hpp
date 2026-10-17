#pragma once
// Internal: one handler overload per command payload. Handlers must not modify the world when
// `exec` is false and must return the same cost in both modes.

#include "sim/commands.hpp"

namespace tp::cmd {

CommandResult handle(World& w, const Command& c, const CmdActivateChunks& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdCreateCompany& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdSetLoan& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdTerraform& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdClearArea& x, bool exec);
CommandResult handle(World& w, const Command& c, const CmdPlantTrees& x, bool exec);

// Helpers shared by handlers.
struct Rect {
    i32 x0, y0, x1, y1; // inclusive
    i64 area() const { return static_cast<i64>(x1 - x0 + 1) * (y1 - y0 + 1); }
};
Rect normalise(TilePos a, TilePos b);

} // namespace tp::cmd
