#include "sim/rail_path.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/rail.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace tp {

u8 railExits(const World& w, TilePos t, Dir entry, DepotId allowDepot) {
    const Tile* tile = w.map.tile(t);
    if (!tile) return 0;
    switch (tile->type) {
    case TileType::Rail: {
        u8 exits = 0;
        const u8 pieces = tile->rail & tracksAtEdge(entry);
        for (u8 bit = 1; bit <= kTrackNW; bit <<= 1) {
            if (!(pieces & bit)) continue;
            const Dir x = otherEdge(bit, entry);
            // One-way signals cannot be passed from behind.
            if (isStraightTrack(bit) && tile->extra != 0 && !(tile->extra & signalFacing(bit, x))) continue;
            exits |= dirBit(x);
        }
        return exits;
    }
    case TileType::Station:
        if (static_cast<StopKind>(tile->extra) != StopKind::Rail) return 0;
        return (tile->rail & tracksAtEdge(entry)) ? dirBit(opposite(entry)) : 0;
    case TileType::Depot:
        // Trains only enter the depot they are heading for; inside it they can turn back out.
        if (DepotId(tile->index) != allowDepot || static_cast<Dir>(tile->extra) != entry) return 0;
        return dirBit(entry);
    default: return 0;
    }
}

std::optional<std::vector<u8>> findRailPath(const World& w, TilePos start, Dir entry, const std::vector<TilePos>& goals,
                                            DepotId allowDepot, int maxNodes) {
    if (goals.empty()) return std::nullopt;
    struct NodeKey {
        i32 x, y;
        u8 e;
        bool operator==(const NodeKey&) const = default;
    };
    struct NodeHash {
        usize operator()(const NodeKey& k) const {
            return static_cast<usize>(mix64((static_cast<u64>(static_cast<u32>(k.x)) << 32) ^ static_cast<u32>(k.y)) ^ k.e);
        }
    };
    struct Info {
        i64 g;
        NodeKey parent;
        TilePos tile;
        Dir entry;
        Dir exitFromParent;
        bool closed;
    };
    struct Open {
        i64 f;
        u64 order;
        NodeKey key;
        bool operator>(const Open& o) const { return f != o.f ? f > o.f : order > o.order; }
    };
    const auto heuristic = [&](TilePos t) {
        i64 best = -1;
        for (const TilePos g : goals) {
            const i64 d = chebyshev(g.x - t.x, g.y - t.y) * 200;
            if (best < 0 || d < best) best = d;
        }
        return best;
    };
    const auto isGoal = [&](TilePos t) { return std::find(goals.begin(), goals.end(), t) != goals.end(); };

    std::unordered_map<NodeKey, Info, NodeHash> info;
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    u64 counter = 0;
    const NodeKey startKey{start.x, start.y, static_cast<u8>(entry)};
    info[startKey] = Info{0, startKey, start, entry, entry, false};
    open.push({heuristic(start), counter++, startKey});
    int expanded = 0;
    while (!open.empty()) {
        const Open cur = open.top();
        open.pop();
        Info& ci = info[cur.key];
        if (ci.closed) continue;
        ci.closed = true;
        if (isGoal(ci.tile)) {
            std::vector<u8> exits;
            NodeKey k = cur.key;
            while (!(k == startKey)) {
                const Info& n = info[k];
                exits.push_back(static_cast<u8>(n.exitFromParent));
                k = n.parent;
            }
            std::reverse(exits.begin(), exits.end());
            return exits;
        }
        if (++expanded > maxNodes) break;
        const TilePos tile = ci.tile;
        const Dir in = ci.entry;
        const i64 gHere = ci.g;
        const u8 exits = railExits(w, tile, in, allowDepot);
        const Tile* t = w.map.tile(tile);
        for (const Dir d : kDirs) {
            if (!(exits & dirBit(d))) continue;
            if (!railLinkOpen(w, tile, d)) continue;
            const TilePos next = neighbour(tile, d);
            i64 step = 256;
            const u8 bit = d == in ? 0 : trackBetween(in, d);
            if (bit && !isStraightTrack(bit)) step += 20;
            if (t && bit && (t->extra2 & bit)) step += 600; // currently reserved by another train
            if (t && t->type == TileType::Station && !isGoal(tile)) step += 300;
            const i64 g = gHere + step;
            const NodeKey nk{next.x, next.y, static_cast<u8>(opposite(d))};
            auto it = info.find(nk);
            if (it != info.end() && (it->second.closed || it->second.g <= g)) continue;
            info[nk] = Info{g, cur.key, next, opposite(d), d, false};
            open.push({g + heuristic(next), counter++, nk});
        }
    }
    return std::nullopt;
}

} // namespace tp
