#include "sim/private_cars.hpp"
#include "sim/world.hpp"
#include "sim/road_path.hpp"
#include "sim/road.hpp"
#include "core/hash.hpp"
#include "core/math.hpp"
#include <algorithm>
#include <deque>
#include <unordered_map>

namespace tp::privatecars {
namespace {
u64 tileKey(TilePos tile) { return ChunkPos{tile.x, tile.y}.key(); }
Dir direction(TilePos a, TilePos b) {
    return b.x > a.x ? Dir::E : b.x < a.x ? Dir::W : b.y > a.y ? Dir::S : Dir::N;
}
std::vector<TilePos> access(const World& w, TownId town, TilePos point2) {
    const TilePos at = tileAt(walking::endpoint(w, point2));
    std::vector<TilePos> result;
    // Start at the building's street entrance; never jump over a block to another road.
    for (int radius = 0; radius <= 1 && result.empty(); ++radius)
        for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
            if (std::abs(x) + std::abs(y) != radius) continue;
            const TilePos tile{at.x + x, at.y + y};
            if (towns::townAt(w, tile) == town && drivablePieces(w, tile, VehicleType::Road, {})) result.push_back(tile);
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
std::optional<std::vector<TilePos>> route(const World& w, TownId town, TilePos from2, TilePos to2) {
    if (!w.towns.contains(town) || towns::townAt(w, {floorDiv(from2.x, 2), floorDiv(from2.y, 2)}) != town ||
        towns::townAt(w, {floorDiv(to2.x, 2), floorDiv(to2.y, 2)}) != town) return std::nullopt;
    const auto starts = access(w, town, from2), goals = access(w, town, to2);
    if (starts.empty() || goals.empty()) return std::nullopt;
    std::deque<TilePos> queue;
    std::unordered_map<u64, TilePos> previous;
    for (auto tile : starts) { previous[tileKey(tile)] = tile; queue.push_back(tile); }
    while (!queue.empty() && previous.size() < 8192) {
        const auto current = queue.front(); queue.pop_front();
        if (std::ranges::find(goals, current) != goals.end()) {
            std::vector<TilePos> path{current};
            while (previous.at(tileKey(path.back())) != path.back()) path.push_back(previous.at(tileKey(path.back())));
            std::reverse(path.begin(), path.end());
            return path.size() >= 2 ? std::optional(std::move(path)) : std::nullopt;
        }
        for (Dir d : kDirs) {
            const auto next = neighbour(current, d);
            if (previous.contains(tileKey(next)) || towns::townAt(w, next) != town ||
                !roadLinkOpen(w, current, d, VehicleType::Road, {})) continue;
            previous.emplace(tileKey(next), current); queue.push_back(next);
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
i32 progress(const Person& p, i64 tick) {
    const auto length = pathLength(entry(p), exit(p));
    return static_cast<i32>(std::clamp<i64>(tick - p.carStepStart, 0, kDriveTicksPerTile) * length / kDriveTicksPerTile);
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
    auto path = route(w, p.town, from2, to2);
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
        people::schedule(w, id, w.clock.tick + kDriveTicksPerTile); return true;
    }
    if (p.carIndex + 1 == p.carPath.size()) { release(w, id); p.carPath.clear(); return false; }
    const auto next = p.carPath[p.carIndex + 1];
    if (!roadLinkOpen(w, current, exit(p), VehicleType::Road, {}) || towns::townAt(w, next) != p.town) {
        // A road was removed: re-plan from the current street, falling back safely if cut off.
        release(w, id); p.carPath.clear();
        if (auto replacement = route(w, p.town, {current.x * 2 + 1, current.y * 2 + 1}, p.tripTo2)) {
            p.carPath = std::move(*replacement); p.carIndex = 0; p.carStepStart = w.clock.tick;
            index(w, id); people::schedule(w, id, w.clock.tick + kDriveTicksPerTile); return true;
        }
        people::travelAbstract(w, id, {current.x * 2 + 1, current.y * 2 + 1}, p.tripTo2, false);
        return true;
    }
    if (blocked(w, id, next, opposite(exit(p)))) { people::schedule(w, id, w.clock.tick + 2); return true; }
    release(w, id); ++p.carIndex; p.carStepStart = w.clock.tick;
    index(w, id);
    people::schedule(w, id, w.clock.tick + kDriveTicksPerTile);
    return true;
}
void rebuild(World& w) {
    w.privateCarTiles.clear();
    for (const auto [id, p] : w.people)
        if (p.state == PersonState::Driving && p.carStarted && p.carIndex < p.carPath.size()) index(w, id);
}
}
