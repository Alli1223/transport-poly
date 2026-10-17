#include "sim/terraform.hpp"

#include "sim/map.hpp"
#include "sim/terrain_gen.hpp"

#include <algorithm>
#include <deque>
#include <map>

namespace tp {

namespace {
u64 cornerKey(i32 cx, i32 cy) { return ChunkPos{cy, cx}.key(); } // (cy, cx) order -> row major sort
} // namespace

TerraformPlan planTerraform(const Map& map, i32 cx, i32 cy, int delta, usize maxCorners) {
    TerraformPlan plan;
    if (delta != 1 && delta != -1) {
        plan.error = "invalid terraform direction";
        return plan;
    }
    std::map<u64, std::pair<TilePos, u8>> target; // corner -> new height
    const auto current = [&](i32 x, i32 y) -> int {
        const auto it = target.find(cornerKey(x, y));
        return it != target.end() ? it->second.second : map.cornerHeight(x, y);
    };

    std::deque<std::pair<TilePos, int>> queue;
    queue.push_back({{cx, cy}, map.cornerHeight(cx, cy) + delta});
    while (!queue.empty()) {
        const auto [c, h] = queue.front();
        queue.pop_front();
        if (h < 0) {
            plan.error = "cannot lower below sea level";
            return plan;
        }
        if (h > kMaxHeight) {
            plan.error = "too high";
            return plan;
        }
        const int cur = current(c.x, c.y);
        if ((delta > 0 && cur >= h) || (delta < 0 && cur <= h)) continue;
        if (!map.cornerEditable(c.x, c.y)) {
            plan.error = "area not explored";
            return plan;
        }
        target[cornerKey(c.x, c.y)] = {c, static_cast<u8>(h)};
        if (target.size() > maxCorners) {
            plan.error = "terraform area too large";
            return plan;
        }
        for (const Dir d : kDirs) {
            const TilePos n = c + dirOffset(d);
            const int hn = current(n.x, n.y);
            if (delta > 0 && h - hn > 1) queue.push_back({n, h - 1});
            if (delta < 0 && hn - h > 1) queue.push_back({n, h + 1});
        }
    }

    plan.changes.reserve(target.size());
    for (const auto& [key, entry] : target) {
        const auto& [pos, h] = entry;
        plan.changes.push_back({pos.x, pos.y, map.cornerHeight(pos.x, pos.y), h});
    }
    plan.ok = true;
    return plan;
}

std::vector<TilePos> terraformAffectedTiles(const TerraformPlan& plan) {
    std::vector<TilePos> tiles;
    tiles.reserve(plan.changes.size() * 4);
    for (const CornerChange& c : plan.changes) {
        tiles.push_back({c.cx - 1, c.cy - 1});
        tiles.push_back({c.cx, c.cy - 1});
        tiles.push_back({c.cx - 1, c.cy});
        tiles.push_back({c.cx, c.cy});
    }
    std::sort(tiles.begin(), tiles.end());
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
    return tiles;
}

void applyTerraform(Map& map, const TerraformPlan& plan) {
    for (const CornerChange& c : plan.changes) map.setCornerHeight(c.cx, c.cy, c.to);
}

} // namespace tp
