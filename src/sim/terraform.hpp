#pragma once
// Raising / lowering terrain corners while maintaining the corner height rule
// (edge-adjacent corners differ by at most one level).

#include "core/types.hpp"
#include "sim/coords.hpp"

#include <string>
#include <vector>

namespace tp {

class Map;

struct CornerChange {
    i32 cx = 0;
    i32 cy = 0;
    u8 from = 0;
    u8 to = 0;
};

struct TerraformPlan {
    bool ok = false;
    std::string error;
    std::vector<CornerChange> changes; // sorted by (cy, cx)
};

// Computes every corner change needed to move corner (cx, cy) by `delta` (+1 or -1) levels,
// propagating to neighbours so that the height rule keeps holding.
TerraformPlan planTerraform(const Map& map, i32 cx, i32 cy, int delta, usize maxCorners = 4096);

// Tiles that have at least one changed corner, sorted and unique.
std::vector<TilePos> terraformAffectedTiles(const TerraformPlan& plan);

void applyTerraform(Map& map, const TerraformPlan& plan);

} // namespace tp
