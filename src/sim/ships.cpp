// Ship movement: ships sail from water tile centre to water tile centre along A* paths, stop at
// the berth in front of a dock to load, and enter their shipyard from the berth in front of it.
// Ships do not block each other.

#include "core/math.hpp"
#include "sim/passengers.hpp"
#include "sim/vehicle.hpp"
#include "sim/water_path.hpp"
#include "sim/world.hpp"

#include <algorithm>

namespace tp::ships {

namespace {

constexpr i32 kAccel = 1;
constexpr i32 kDecel = 3;
constexpr i64 kBaseDwellTicks = 5 * kTicksPerMinute;
constexpr i32 kRetryTicks = 64; // after failing to find a route

std::vector<TilePos> goalsFor(const World& w, const Order& o) {
    std::vector<TilePos> goals;
    if (o.kind == OrderKind::Station) {
        if (const Station* s = w.stations.tryGet(o.station)) {
            for (const TilePos t : s->tiles) {
                const Tile* tile = w.map.tile(t);
                if (!tile || tile->type != TileType::Station || static_cast<StopKind>(tile->extra) != StopKind::Dock)
                    continue;
                const TilePos b = berth(w, t);
                if (isWaterTile(w, b)) goals.push_back(b);
            }
        }
    } else if (const Depot* d = w.depots.tryGet(o.depot); d && d->kind == DepotKind::Ship) {
        const TilePos b = neighbour(d->tile, d->facing);
        if (isWaterTile(w, b)) goals.push_back(b);
    }
    return goals;
}

void enterDepot(World& w, VehicleId id, DepotId did) {
    Vehicle& v = w.vehicles[id];
    for (const PersonId p : v.passengers) passengers::abandonRide(w, p);
    v.passengers.clear();
    v.state = VehicleState::InDepot;
    v.speed = 0;
    v.progress = 0;
    v.depot = did;
    v.path.clear();
    v.pathIndex = 0;
    if (const Depot* d = w.depots.tryGet(did)) {
        v.tile = d->tile;
        v.entry = d->facing;
        v.exit = opposite(d->facing);
    }
    vehicles::arriveInDepot(w, id);
}

void arriveAtDock(World& w, VehicleId id, StationId sid) {
    Vehicle& v = w.vehicles[id];
    v.state = VehicleState::Loading;
    v.speed = 0;
    const i32 exchanged = passengers::exchangeAtStop(w, id, sid);
    v.loadingUntil = w.clock.tick + kBaseDwellTicks + exchanged / 3;
    stations::recordVisit(w, sid, id);
}

// At a tile centre: arrive if this is the goal, otherwise pick the next heading (replanning when
// needed). Returns false when the ship has to stay put.
bool atCentre(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const auto o = targetOrder(v);
    if (!o) return false;
    const std::vector<TilePos> goals = goalsFor(w, *o);
    if (std::ranges::find(goals, v.tile) != goals.end()) {
        v.path.clear();
        v.pathIndex = 0;
        if (o->kind == OrderKind::Depot)
            enterDepot(w, id, o->depot);
        else
            arriveAtDock(w, id, o->station);
        return false;
    }
    if (v.pathIndex >= v.path.size() || !canSail(w, v.tile, v.path[v.pathIndex])) {
        if (v.retryTicks > 0) return false;
        auto path = findWaterPath(w, v.tile, goals);
        if (!path || path->empty()) {
            v.lost = true;
            v.retryTicks = kRetryTicks;
            v.path.clear();
            v.pathIndex = 0;
            return false;
        }
        v.lost = false;
        v.path = std::move(*path);
        v.pathIndex = 0;
    }
    v.prevHeading = v.heading;
    v.heading = v.path[v.pathIndex++];
    return true;
}

// Distance (units) left to sail along the planned path, capped at `cap`.
i32 remainingDistance(const Vehicle& v, i32 cap) {
    i32 d = headingLength(v.heading) - v.progress;
    for (usize i = v.pathIndex; i < v.path.size() && d < cap; ++i) d += headingLength(v.path[i]);
    return std::min(d, cap);
}

} // namespace

TilePos berth(const World& w, TilePos dock) {
    const Tile* t = w.map.tile(dock);
    return t ? neighbour(dock, static_cast<Dir>(t->extra2 & 3)) : dock;
}

bool leaveDepot(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const Depot* d = w.depots.tryGet(v.depot);
    if (!d) return false;
    const TilePos b = neighbour(d->tile, d->facing);
    if (!isWaterTile(w, b)) return false;
    v.state = VehicleState::Driving;
    v.tile = b;
    v.progress = 0;
    v.speed = 0;
    v.heading = headingOf(d->facing);
    v.prevHeading = v.heading;
    v.path.clear();
    v.pathIndex = 0;
    v.retryTicks = 0;
    v.moving = false;
    return true;
}

void tick(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    if (v.retryTicks > 0) --v.retryTicks;
    if (!v.moving) {
        if (!atCentre(w, id)) {
            v.speed = 0;
            return;
        }
        v.moving = true;
    }
    const i32 maxSpeed = std::max(1, speedUnits(v.info().maxSpeedKmh));
    const bool lastLeg = v.pathIndex >= v.path.size();
    i32 desired = maxSpeed;
    if (lastLeg || v.path.size() - v.pathIndex < 4) {
        // Slow down towards the end of the route.
        const i32 rem = remainingDistance(v, 4096);
        desired = std::min(desired, std::max(3, static_cast<i32>(isqrt(static_cast<u64>(2 * kDecel * rem)))));
    }
    if (v.speed < desired)
        v.speed = std::min(desired, v.speed + kAccel);
    else
        v.speed = std::max(desired, v.speed - kDecel);
    v.progress += v.speed;
    for (int guard = 0; guard < 4 && v.progress >= headingLength(v.heading); ++guard) {
        v.progress -= headingLength(v.heading);
        v.tile = headingStep(v.tile, v.heading);
        if (!atCentre(w, id)) {
            v.progress = 0;
            v.moving = false;
            if (v.state == VehicleState::Driving) v.speed = std::min(v.speed, 3);
            return;
        }
    }
}

} // namespace tp::ships
