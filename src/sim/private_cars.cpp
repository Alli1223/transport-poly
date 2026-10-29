#include "sim/private_cars.hpp"
#include "sim/world.hpp"
#include "sim/road_path.hpp"
#include "sim/road.hpp"
#include "core/hash.hpp"
#include "core/math.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>

namespace tp::privatecars {
namespace {
u64 tileKey(TilePos tile) { return ChunkPos{tile.x, tile.y}.key(); }
Dir direction(TilePos a, TilePos b) {
    return b.x > a.x ? Dir::E : b.x < a.x ? Dir::W : b.y > a.y ? Dir::S : Dir::N;
}
std::vector<TilePos> access(const World& w, TilePos point2) {
    const TilePos at = tileAt(walking::endpoint(w, point2));
    std::vector<TilePos> result;
    // Start at the building's street entrance; never jump over a block to another road.
    for (int radius = 0; radius <= 1 && result.empty(); ++radius)
        for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
            if (std::abs(x) + std::abs(y) != radius) continue;
            const TilePos tile{at.x + x, at.y + y};
            if (drivablePieces(w, tile, VehicleType::Road, {})) result.push_back(tile);
        }
    return result;
}
bool blocked(const World& w, PersonId id, TilePos tile, Dir entering) {
    const auto* t = w.map.tile(tile);
    if (!t) return true;
    if (t->type == TileType::Road && ((t->rail && (t->extra2 & t->rail)) || track::crossingClosed(w, tile))) return true;
    if (auto it = w.privateCarTiles.find(tileKey(tile)); it != w.privateCarTiles.end())
        for (const PersonId other : it->second) {
            const auto* p = w.people.tryGet(other);
            if (other != id && p && !p->carPath.empty() && p->carIndex < p->carPath.size() &&
                p->carStepStart <= w.clock.tick && entry(*p) == entering) return true;
        }
    if (auto it = w.vehicleTiles.find(tileKey(tile)); it != w.vehicleTiles.end())
        for (const VehicleId other : it->second) {
            const auto* v = w.vehicles.tryGet(other);
            if (v && v->tile == tile && v->state != VehicleState::InDepot && v->info().type != VehicleType::Ship &&
                (v->entry == entering || v->info().type == VehicleType::Rail)) return true;
        }
    return false;
}
void index(World& w, PersonId id) {
    const auto& p = w.people[id];
    if (p.carIndex < p.carPath.size()) w.privateCarTiles[tileKey(p.carPath[p.carIndex])].push_back(id);
}
}
bool ownsCar(u32 serial, i32 year) {
    const auto percent = static_cast<u32>(iclamp(12 + (year - 1950) * 6 / 5, 12, 75));
    return mix32(serial * 0x9e3779b9u) % 100 < percent;
}
std::optional<std::vector<TilePos>> route(const World& w, TilePos from2, TilePos to2) {
    const auto starts = access(w, from2), goals = access(w, to2);
    if (starts.empty() || goals.empty()) return std::nullopt;
    // A* over every connected road, in towns and across the countryside between them. Ties break
    // on insertion order, so every peer finds the same path.
    const auto estimate = [&](TilePos t) {
        i64 best = std::numeric_limits<i64>::max();
        for (const TilePos g : goals) best = std::min<i64>(best, std::abs(t.x - g.x) + std::abs(t.y - g.y));
        return best;
    };
    struct Open {
        i64 f, cost;
        u32 order;
        TilePos tile;
        bool operator>(const Open& o) const { return f != o.f ? f > o.f : order > o.order; }
    };
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    std::unordered_map<u64, std::pair<TilePos, i64>> best; // tile -> (previous tile, cost)
    u32 order = 0;
    for (const TilePos tile : starts) {
        best[tileKey(tile)] = {tile, 0};
        open.push({estimate(tile), 0, order++, tile});
    }
    for (i32 expanded = 0; !open.empty() && expanded < kMaxRouteTiles; ++expanded) {
        const Open current = open.top();
        open.pop();
        if (current.cost > best.at(tileKey(current.tile)).second) continue; // superseded
        if (std::ranges::find(goals, current.tile) != goals.end()) {
            std::vector<TilePos> path{current.tile};
            while (best.at(tileKey(path.back())).first != path.back()) path.push_back(best.at(tileKey(path.back())).first);
            std::reverse(path.begin(), path.end());
            return path.size() >= 2 ? std::optional(std::move(path)) : std::nullopt;
        }
        for (const Dir d : kDirs) {
            if (!roadLinkOpen(w, current.tile, d, VehicleType::Road, {})) continue;
            const TilePos next = neighbour(current.tile, d);
            const i64 cost = current.cost + 1;
            const auto [it, fresh] = best.try_emplace(tileKey(next), current.tile, cost);
            if (!fresh) {
                if (it->second.second <= cost) continue;
                it->second = {current.tile, cost};
            }
            open.push({cost + estimate(next), cost, order++, next});
        }
    }
    return std::nullopt;
}
Dir entry(const Person& p) {
    if (p.carIndex > 0 && p.carIndex < p.carPath.size()) return direction(p.carPath[p.carIndex], p.carPath[p.carIndex - 1]);
    return p.carPath.size() > 1 ? opposite(direction(p.carPath[0], p.carPath[1])) : Dir::W;
}
Dir exit(const Person& p) {
    if (p.carIndex + 1 < p.carPath.size()) return direction(p.carPath[p.carIndex], p.carPath[p.carIndex + 1]);
    return opposite(entry(p));
}
i64 stepTicks(const World& w, const Person& p) {
    if (p.carIndex >= p.carPath.size()) return kDriveTicksPerTile;
    const TilePos tile = p.carPath[p.carIndex];
    if (!roadcurves::follow(w, tile, entry(p), exit(p)).curve) return kDriveTicksPerTile;
    // Along a curved road, at the same pace however much of the curve the tile holds.
    const i64 length = roadcurves::pathLength(w, tile, entry(p), exit(p));
    return std::clamp<i64>((length * kDriveTicksPerTile + kTrackUnits / 2) / kTrackUnits, 1, 3 * kDriveTicksPerTile);
}
i32 progress(const World& w, const Person& p, i64 tick) {
    if (p.carIndex >= p.carPath.size()) return 0;
    const auto length = roadcurves::pathLength(w, p.carPath[p.carIndex], entry(p), exit(p));
    const i64 ticks = stepTicks(w, p);
    return static_cast<i32>(std::clamp<i64>(tick - p.carStepStart, 0, ticks) * length / ticks);
}
void release(World& w, PersonId id) {
    auto& p = w.people[id];
    if (p.carIndex < p.carPath.size()) {
        const auto key = tileKey(p.carPath[p.carIndex]);
        if (auto it = w.privateCarTiles.find(key); it != w.privateCarTiles.end()) {
            std::erase(it->second, id);
            if (it->second.empty()) w.privateCarTiles.erase(it);
        }
    }
}
bool begin(World& w, PersonId id, TilePos from2, TilePos to2) {
    auto& p = w.people[id];
    if (!p.carOwner) return false;
    auto path = route(w, from2, to2);
    if (!path) return false;
    release(w, id);
    p.carPath = std::move(*path); p.carIndex = 0;
    p.carStepStart = w.clock.tick + kDriveOverheadTicks;
    p.carStarted = false;
    p.state = PersonState::Driving;
    people::schedule(w, id, p.carStepStart);
    return true;
}
bool advance(World& w, PersonId id) {
    auto& p = w.people[id];
    if (p.carPath.empty() || p.carIndex >= p.carPath.size()) return false; // legacy abstract journey
    const auto current = p.carPath[p.carIndex];
    if (!p.carStarted) {
        if (blocked(w, id, current, entry(p))) {
            p.carStepStart = w.clock.tick + 2;
            people::schedule(w, id, p.carStepStart); return true;
        }
        p.carStarted = true; p.carStepStart = w.clock.tick; index(w, id);
        people::schedule(w, id, w.clock.tick + stepTicks(w, p)); return true;
    }
    if (p.carIndex + 1 == p.carPath.size()) { release(w, id); p.carPath.clear(); return false; }
    const auto next = p.carPath[p.carIndex + 1];
    if (!roadLinkOpen(w, current, exit(p), VehicleType::Road, {})) {
        // A road was removed: re-plan from the current street, falling back safely if cut off.
        release(w, id); p.carPath.clear();
        if (auto replacement = route(w, {current.x * 2 + 1, current.y * 2 + 1}, p.tripTo2)) {
            p.carPath = std::move(*replacement); p.carIndex = 0; p.carStepStart = w.clock.tick;
            index(w, id); people::schedule(w, id, w.clock.tick + stepTicks(w, p)); return true;
        }
        people::travelAbstract(w, id, {current.x * 2 + 1, current.y * 2 + 1}, p.tripTo2, false);
        return true;
    }
    if (blocked(w, id, next, opposite(exit(p)))) { people::schedule(w, id, w.clock.tick + 2); return true; }
    release(w, id); ++p.carIndex; p.carStepStart = w.clock.tick;
    index(w, id);
    people::schedule(w, id, w.clock.tick + stepTicks(w, p));
    return true;
}
void rebuild(World& w) {
    w.privateCarTiles.clear();
    for (const auto [id, p] : w.people)
        if (p.state == PersonState::Driving && p.carStarted && p.carIndex < p.carPath.size()) index(w, id);
}
}
