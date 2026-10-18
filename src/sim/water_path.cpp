#include "sim/water_path.hpp"

#include "sim/world.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <queue>
#include <unordered_map>

namespace tp {

bool isWaterTile(const World& w, TilePos t) {
    const Tile* tile = w.map.tile(t);
    return tile && tile->type == TileType::Water;
}

bool canSail(const World& w, TilePos from, u8 heading) {
    if (!isWaterTile(w, headingStep(from, heading))) return false;
    if (heading & 1)
        return isWaterTile(w, {from.x + kHeadingDX[heading], from.y}) &&
               isWaterTile(w, {from.x, from.y + kHeadingDY[heading]});
    return true;
}

std::optional<std::vector<u8>> findWaterPath(const World& w, TilePos from, std::span<const TilePos> goals,
                                             i32 maxNodes) {
    if (goals.empty()) return std::nullopt;
    if (std::ranges::find(goals, from) != goals.end()) return std::vector<u8>{};
    // Octile distance in tenths of a tile to the closest goal.
    const auto heuristic = [&](TilePos t) {
        i32 best = INT_MAX;
        for (const TilePos g : goals) {
            const i32 dx = std::abs(g.x - t.x), dy = std::abs(g.y - t.y);
            best = std::min(best, 10 * std::max(dx, dy) + 4 * std::min(dx, dy));
        }
        return best;
    };
    struct Node {
        i32 g;
        u8 heading; // heading used to arrive here
        bool closed;
    };
    struct Open {
        i32 f;
        u32 order;
        TilePos t;
        bool operator<(const Open& o) const { return f != o.f ? f > o.f : order > o.order; }
    };
    const auto key = [](TilePos t) { return ChunkPos{t.x, t.y}.key(); };
    std::unordered_map<u64, Node> nodes;
    std::priority_queue<Open> open;
    u32 order = 0;
    nodes[key(from)] = Node{0, 0xFF, false};
    open.push({heuristic(from), order++, from});
    i32 expanded = 0;
    while (!open.empty()) {
        const Open cur = open.top();
        open.pop();
        Node& n = nodes[key(cur.t)];
        if (n.closed) continue;
        n.closed = true;
        if (std::ranges::find(goals, cur.t) != goals.end()) {
            std::vector<u8> path;
            for (TilePos t = cur.t; t != from;) {
                const u8 h = nodes[key(t)].heading;
                path.push_back(h);
                t = headingStep(t, static_cast<u8>((h + 4) & 7));
            }
            std::ranges::reverse(path);
            return path;
        }
        if (++expanded > maxNodes) break;
        const i32 g0 = n.g;
        for (u8 h = 0; h < 8; ++h) {
            if (!canSail(w, cur.t, h)) continue;
            const TilePos next = headingStep(cur.t, h);
            const i32 g = g0 + ((h & 1) ? 14 : 10);
            auto [it, inserted] = nodes.try_emplace(key(next), Node{g, h, false});
            if (!inserted) {
                if (it->second.closed || it->second.g <= g) continue;
                it->second.g = g;
                it->second.heading = h;
            }
            open.push({g + heuristic(next), order++, next});
        }
    }
    return std::nullopt;
}

} // namespace tp
