#pragma once
// Raising / lowering terrain corners while maintaining the corner height rule
// (edge-adjacent corners differ by at most one level).

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/map.hpp"
#include "sim/terrain_gen.hpp"

#include <algorithm>
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

// Smooth terrain brush (Transport Fever style): shapes all corners within `radius` tiles of a
// corner with a soft falloff. Corners touching structures, owned water or unexplored land are
// left alone.
enum class BrushMode : u8 { Raise, Lower, Level, Smooth };

struct FineChange {
    i32 cx = 0;
    i32 cy = 0;
    u16 from = 0;
    u16 to = 0;
};

struct BrushPlan {
    std::vector<FineChange> changes; // row major
    i64 volume = 0;                  // sum of |height change| in fine units
};

// `locked(cx, cy)` reports corners that must not move.
template <class Locked>
BrushPlan planBrush(const Map& map, TilePos centre, i32 radius, BrushMode mode, u16 target, Locked&& locked);
void applyBrush(Map& map, const BrushPlan& plan);

template <class Locked>
BrushPlan planBrush(const Map& map, TilePos centre, i32 radius, BrushMode mode, u16 target, Locked&& locked) {
    BrushPlan plan;
    const i32 r = radius < 1 ? 1 : (radius > 12 ? 12 : radius);
    const i64 r2 = static_cast<i64>(r) * r;
    constexpr i32 kMax = kMaxSmoothHeight * kSubLevels;
    for (i32 dy = -r; dy <= r; ++dy) {
        for (i32 dx = -r; dx <= r; ++dx) {
            const i64 d2 = static_cast<i64>(dx) * dx + static_cast<i64>(dy) * dy;
            if (d2 > r2) continue;
            const i32 cx = centre.x + dx, cy = centre.y + dy;
            if (!map.cornerEditable(cx, cy) || locked(cx, cy)) continue;
            // Quadratic falloff in [0, 1024].
            const i64 lin = (r2 - d2) * 1024 / r2;
            const i64 w = lin * lin / 1024;
            const i32 old = map.cornerFine(cx, cy);
            i64 next = old;
            switch (mode) {
            case BrushMode::Raise: next = old + std::max<i64>(w > 0 ? 1 : 0, kSubLevels * w / 1024); break;
            case BrushMode::Lower: next = old - std::max<i64>(w > 0 ? 1 : 0, kSubLevels * w / 1024); break;
            case BrushMode::Level: next = old + (static_cast<i64>(target) - old) * std::min<i64>(1024, w * 2) / 1024; break;
            case BrushMode::Smooth: {
                const i64 avg = (static_cast<i64>(map.cornerFine(cx - 1, cy)) + map.cornerFine(cx + 1, cy) +
                                 map.cornerFine(cx, cy - 1) + map.cornerFine(cx, cy + 1)) /
                                4;
                next = old + (avg - old) * w / 1536;
                break;
            }
            }
            next = next < 0 ? 0 : (next > kMax ? kMax : next);
            if (next == old) continue;
            plan.changes.push_back({cx, cy, static_cast<u16>(old), static_cast<u16>(next)});
            plan.volume += next > old ? next - old : old - next;
        }
    }
    return plan;
}

} // namespace tp
