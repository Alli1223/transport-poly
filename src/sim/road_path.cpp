#include "sim/road_path.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace tp {

namespace {

u8 piecesFor(const Tile& t, VehicleType type) {
    return type == VehicleType::Tram ? tramPieces(t) : roadPieces(t);
}

bool isStopFor(const Tile& t, VehicleType type) {
    const StopKind k = static_cast<StopKind>(t.extra);
    return type == VehicleType::Tram ? k == StopKind::Tram : k == StopKind::Bus;
}

} // namespace

u8 drivablePieces(const World& w, TilePos t, VehicleType type, DepotId allowDepot) {
    const Tile* tile = w.map.tile(t);
    if (!tile) return 0;
    switch (tile->type) {
    case TileType::Road: return piecesFor(*tile, type);
    case TileType::Station: return isStopFor(*tile, type) ? piecesFor(*tile, type) : 0;
    case TileType::Depot: {
        if (DepotId(tile->index) != allowDepot) return 0;
        const Depot* d = w.depots.tryGet(allowDepot);
        if (!d) return 0;
        const bool kindOk = type == VehicleType::Tram ? d->kind == DepotKind::Tram : d->kind == DepotKind::Road;
        return kindOk ? dirBit(d->facing) : 0;
    }
    default: return 0;
    }
}

bool roadLinkOpen(const World& w, TilePos a, Dir d, VehicleType type, DepotId allowDepot) {
    const u8 pa = drivablePieces(w, a, type, allowDepot);
    if (!(pa & dirBit(d))) return false;
    const TilePos b = neighbour(a, d);
    const u8 pb = drivablePieces(w, b, type, allowDepot);
    const Dir od = opposite(d);
    if (!(pb & dirBit(od))) return false;
    // Heights must match at the shared edge (depots / stops count with their own pieces).
    const Tile* ta = w.map.tile(a);
    const Tile* tb = w.map.tile(b);
    const u8 rpa = ta->type == TileType::Depot ? pa : static_cast<u8>(ta->road & kRoadPieceMask) | pa;
    const u8 rpb = tb->type == TileType::Depot ? pb : static_cast<u8>(tb->road & kRoadPieceMask) | pb;
    return transportEdgeHeight(w.map, a, rpa, d) == transportEdgeHeight(w.map, b, rpb, od);
}

std::optional<std::vector<u8>> findRoadPath(const World& w, TilePos start, Dir entry, const std::vector<TilePos>& goals,
                                            VehicleType type, DepotId allowDepot, int maxNodes) {
    if (goals.empty()) return std::nullopt;
    // Node identity: tile plus entry edge. Keys index the map only; it is never iterated.
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
    const auto nodeKey = [](TilePos t, Dir e) { return NodeKey{t.x, t.y, static_cast<u8>(e)}; };
    const auto heuristic = [&](TilePos t) {
        i64 best = -1;
        for (const TilePos g : goals) {
            const i64 d = manhattan(g.x - t.x, g.y - t.y) * 256;
            if (best < 0 || d < best) best = d;
        }
        return best;
    };
    const auto isGoal = [&](TilePos t) { return std::find(goals.begin(), goals.end(), t) != goals.end(); };

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
    std::unordered_map<NodeKey, Info, NodeHash> info;
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    u64 counter = 0;
    const NodeKey startKey = nodeKey(start, entry);
    info[startKey] = Info{0, startKey, start, entry, entry, false};
    open.push({heuristic(start), counter++, startKey});

    int expanded = 0;
    while (!open.empty()) {
        const Open cur = open.top();
        open.pop();
        Info& ci = info[cur.key];
        if (ci.closed) continue;
        ci.closed = true;
        const TilePos tile = ci.tile;
        const Dir in = ci.entry;
        if (isGoal(tile) && cur.key != startKey) {
            // Reconstruct exits from start to the goal tile.
            std::vector<u8> exits;
            NodeKey k = cur.key;
            while (k != startKey) {
                const Info& n = info[k];
                exits.push_back(static_cast<u8>(n.exitFromParent));
                k = n.parent;
            }
            std::reverse(exits.begin(), exits.end());
            return exits;
        }
        if (isGoal(tile) && cur.key == startKey) return std::vector<u8>{};
        if (++expanded > maxNodes) break;

        const u8 pieces = drivablePieces(w, tile, type, allowDepot);
        u8 exits = static_cast<u8>(pieces & ~dirBit(in));
        bool uturn = false;
        if (exits == 0 && (pieces & dirBit(in))) {
            exits = dirBit(in); // dead end: turn around
            uturn = true;
        }
        const i64 gHere = ci.g;
        for (const Dir d : kDirs) {
            if (!(exits & dirBit(d))) continue;
            if (!roadLinkOpen(w, tile, d, type, allowDepot)) continue;
            const TilePos next = neighbour(tile, d);
            const Dir nextEntry = opposite(d);
            i64 step = 256;
            if (uturn) step += 1024;
            else if (const roadcurves::Place on = roadcurves::follow(w, tile, in, d); on.curve)
                step = roadcurves::pathLength(w, tile, in, d); // along a curved road: its length, no corners
            else if (d == rotateCW(opposite(in))) step += 24;
            else if (d != opposite(in)) step += 48;
            const i64 g = gHere + step;
            const NodeKey nk = nodeKey(next, nextEntry);
            auto it = info.find(nk);
            if (it != info.end() && (it->second.closed || it->second.g <= g)) continue;
            info[nk] = Info{g, cur.key, next, nextEntry, d, false};
            open.push({g + heuristic(next), counter++, nk});
        }
    }
    return std::nullopt;
}

} // namespace tp
