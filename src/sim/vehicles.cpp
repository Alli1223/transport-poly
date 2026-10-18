#include "sim/vehicle.hpp"

#include "sim/passengers.hpp"
#include "sim/road_path.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <climits>
#include <format>

namespace tp::vehicles {

namespace {

constexpr i32 kFollowGap = 176;        // centre to centre distance kept behind the vehicle ahead
constexpr i32 kGridlockTicks = 600;    // after this long blocked, drive through other vehicles
constexpr i64 kBaseDwellTicks = 3 * kTicksPerMinute;

u64 tileKey(TilePos t) { return ChunkPos{t.x, t.y}.key(); }

Expense runningCategory(VehicleType t) {
    switch (t) {
    case VehicleType::Rail: return Expense::TrainRunning;
    case VehicleType::Ship: return Expense::ShipRunning;
    default: return Expense::RoadVehicleRunning;
    }
}

bool stopMatches(const Tile& t, VehicleType type) {
    const StopKind k = static_cast<StopKind>(t.extra);
    return type == VehicleType::Tram ? k == StopKind::Tram : k == StopKind::Bus;
}

DepotId allowedDepot(const Vehicle& v) {
    const auto o = targetOrder(v);
    return o && o->kind == OrderKind::Depot ? o->depot : v.depot;
}

std::vector<TilePos> goalsFor(const World& w, const Vehicle& v, const Order& o) {
    std::vector<TilePos> goals;
    const VehicleType type = v.info().type;
    if (o.kind == OrderKind::Via) {
        goals.push_back(o.waypoint);
    } else if (o.kind == OrderKind::Station) {
        if (const Station* s = w.stations.tryGet(o.station)) {
            for (const TilePos t : s->tiles) {
                const Tile* tile = w.map.tile(t);
                if (tile && tile->type == TileType::Station && tile->index == o.station.value && stopMatches(*tile, type))
                    goals.push_back(t);
            }
        }
    } else if (const Depot* d = w.depots.tryGet(o.depot)) {
        goals.push_back(d->tile);
    }
    return goals;
}

bool isStopTarget(const World& w, const Vehicle& v, TilePos t) {
    const auto o = targetOrder(v);
    if (!o || o->kind != OrderKind::Station || o->passThrough()) return false;
    const Tile* tile = w.map.tile(t);
    return tile && tile->type == TileType::Station && tile->index == o->station.value && stopMatches(*tile, v.info().type);
}

void advanceOrder(Vehicle& v) {
    if (!v.orders.empty()) v.orderIndex = static_cast<u8>((v.orderIndex + 1) % v.orders.size());
    v.path.clear();
    v.pathIndex = 0;
}

// Chooses the exit to take on the tile the vehicle has just entered.
Dir chooseExit(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const VehicleType type = v.info().type;
    const DepotId allow = allowedDepot(v);
    const auto valid = [&](Dir d) { return roadLinkOpen(w, v.tile, d, type, allow); };

    if (v.pathIndex < v.path.size()) {
        const Dir d = static_cast<Dir>(v.path[v.pathIndex] & 3);
        const u8 pieces = drivablePieces(w, v.tile, type, allow);
        const bool uturnOk = d == v.entry && (pieces & ~dirBit(v.entry)) == 0;
        if (valid(d) || uturnOk) {
            ++v.pathIndex;
            return d;
        }
    }
    // (Re)plan from here.
    v.path.clear();
    v.pathIndex = 0;
    v.tilesSincePlan = 0;
    if (const auto o = targetOrder(v)) {
        const std::vector<TilePos> goals = goalsFor(w, v, *o);
        if (auto path = findRoadPath(w, v.tile, v.entry, goals, type, allow)) {
            v.lost = false;
            if (!path->empty()) {
                v.path = std::move(*path);
                v.pathIndex = 1;
                return static_cast<Dir>(v.path[0] & 3);
            }
            // Already on a goal tile: just carry on through it.
        } else {
            v.lost = !goals.empty();
        }
    }
    // No route: keep driving, preferring straight on, then right, then left, else turn around.
    const Dir travel = opposite(v.entry);
    for (const Dir d : {travel, rotateCW(travel), rotateCCW(travel)})
        if (valid(d)) return d;
    return v.entry;
}

void dropAll(World& w, Vehicle& v) {
    for (const PersonId p : v.passengers) passengers::abandonRide(w, p);
    v.passengers.clear();
}

void enterDepot(World& w, VehicleId id, DepotId did) {
    Vehicle& v = w.vehicles[id];
    const Depot& d = w.depots[did];
    dropAll(w, v);
    v.state = VehicleState::InDepot;
    v.tile = d.tile;
    v.entry = d.facing;
    v.exit = opposite(d.facing);
    v.progress = 0;
    v.speed = 0;
    v.depot = did;
    v.stopAtMiddle = false;
    v.path.clear();
    v.pathIndex = 0;
    arriveInDepot(w, id);
}

bool tileBusyNear(const World& w, TilePos t, VehicleId self) {
    const auto it = w.vehicleTiles.find(tileKey(t));
    if (it == w.vehicleTiles.end()) return false;
    for (const VehicleId o : it->second)
        if (o != self) return true;
    return false;
}

void leaveDepot(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const Depot* d = w.depots.tryGet(v.depot);
    if (!d) return;
    const VehicleType type = v.info().type;
    if (!roadLinkOpen(w, d->tile, d->facing, type, v.depot)) return;
    if (tileBusyNear(w, d->tile, id)) return;
    v.state = VehicleState::Driving;
    v.tile = d->tile;
    v.entry = opposite(d->facing);
    v.exit = d->facing;
    v.progress = kPathStraight / 2;
    v.speed = 0;
    v.stopAtMiddle = false;
    v.blockedTicks = 0;
    v.path.clear();
    v.pathIndex = 0;
    w.vehicleTiles[tileKey(v.tile)].push_back(id);
}

void arriveAtStop(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    v.state = VehicleState::Loading;
    v.speed = 0;
    const auto o = targetOrder(v);
    const StationId sid = o ? o->station : StationId{};
    const i32 exchanged = passengers::exchangeAtStop(w, id, sid);
    v.loadingUntil = w.clock.tick + kBaseDwellTicks + exchanged * 2 + 8;
    stations::recordVisit(w, sid, id);
}

void finishLoading(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const auto o = targetOrder(v);
    if (o && (o->flags & kOrderFullLoad) && v.passengers.size() < v.capacity() &&
        w.clock.tick < v.loadingUntil + 8 * kTicksPerHour) {
        // Keep boarding newcomers while waiting for a full load (gives up after eight hours).
        const i32 n = passengers::exchangeAtStop(w, id, o->station);
        if (n > 0) v.loadingUntil = w.clock.tick + n * 2 + 8;
        if (v.passengers.size() < v.capacity() || n > 0) return;
    }
    v.state = VehicleState::Driving;
    v.stopAtMiddle = false;
    advanceOrder(v);
}

i32 gapAhead(const World& w, VehicleId id) {
    const Vehicle& v = w.vehicles[id];
    i32 best = INT_MAX;
    const i32 len = pathLength(v.entry, v.exit);
    if (const auto it = w.vehicleTiles.find(tileKey(v.tile)); it != w.vehicleTiles.end()) {
        for (const VehicleId oid : it->second) {
            if (oid == id) continue;
            const Vehicle& o = w.vehicles[oid];
            if (o.info().type == VehicleType::Ship) continue; // boats may pass below a bridge
            if (o.entry == v.entry && o.exit == v.exit) {
                if (o.progress > v.progress) best = std::min(best, o.progress - v.progress);
            } else if (o.exit == v.exit) {
                const i32 remO = pathLength(o.entry, o.exit) - o.progress;
                const i32 remV = len - v.progress;
                if (remO < remV) best = std::min(best, remV - remO);
            }
        }
    }
    const TilePos next = neighbour(v.tile, v.exit);
    const Dir ne = opposite(v.exit);
    if (const auto it = w.vehicleTiles.find(tileKey(next)); it != w.vehicleTiles.end()) {
        for (const VehicleId oid : it->second) {
            if (oid == id) continue;
            const Vehicle& o = w.vehicles[oid];
            if (o.info().type == VehicleType::Ship) continue;
            if (o.entry == ne) best = std::min(best, (len - v.progress) + o.progress);
        }
    }
    return best;
}

void drive(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const VehicleModel& m = v.info();
    const i32 maxSpeed = speedUnits(m.maxSpeedKmh);
    i32 desired = maxSpeed;
    const TurnKind tk = turnKind(v.entry, v.exit);
    if (tk == TurnKind::Left || tk == TurnKind::Right) desired = std::min(desired, std::max(10, maxSpeed * 55 / 100));
    if (tk == TurnKind::UTurn) desired = std::min(desired, 8);
    if (v.stopAtMiddle) {
        const i32 remaining = pathLength(v.entry, v.exit) / 2 - v.progress;
        desired = std::min(desired, std::max(4, remaining / 3));
    }
    const i32 gap = v.blockedTicks > kGridlockTicks ? INT_MAX : gapAhead(w, id);
    if (gap != INT_MAX) {
        const i32 room = gap - kFollowGap;
        if (room <= 0) desired = 0;
        else desired = std::min(desired, std::max(1, room / 2));
    }
    if (v.speed < desired) v.speed = std::min(desired, v.speed + 2);
    else v.speed = std::max(desired, v.speed - 6);
    if (v.speed == 0 && desired == 0) ++v.blockedTicks;

    // A rail reservation closes a crossing. This is a hard stop, including after
    // the road gridlock timeout; pathfinding still treats the road as connected.
    const auto closedCrossing = [&](TilePos pos) {
        const Tile* tile = w.map.tile(pos);
        return tile && tile->type == TileType::Road && tile->rail && (tile->extra2 & tile->rail);
    };
    if (closedCrossing(neighbour(v.tile, v.exit)))
        v.speed = std::min(v.speed, std::max(0, pathLength(v.entry, v.exit) - v.progress - 1));

    v.progress += v.speed;
    for (int guard = 0; guard < 4; ++guard) {
        const i32 len = pathLength(v.entry, v.exit);
        if (v.stopAtMiddle && v.progress >= len / 2) {
            v.progress = len / 2;
            arriveAtStop(w, id);
            return;
        }
        if (v.progress < len) return;
        if (closedCrossing(neighbour(v.tile, v.exit))) {
            v.progress = len - 1;
            v.speed = 0;
            return;
        }
        v.progress -= len;
        const VehicleType type = m.type;
        const DepotId allow = allowedDepot(v);
        if (!roadLinkOpen(w, v.tile, v.exit, type, allow) && v.exit != v.entry) {
            // The road ahead vanished: turn around on this tile.
            v.exit = v.entry;
            v.progress = 0;
            v.path.clear();
            v.pathIndex = 0;
            return;
        }
        if (v.exit == v.entry && !roadLinkOpen(w, v.tile, v.exit, type, allow)) {
            v.progress = 0;
            return; // stranded on an isolated tile
        }
        v.tile = neighbour(v.tile, v.exit);
        v.entry = opposite(v.exit);
        ++v.tilesSincePlan;
        if (v.blockedTicks > kGridlockTicks) v.blockedTicks = kGridlockTicks / 2;
        else v.blockedTicks = 0;
        const Tile* tile = w.map.tile(v.tile);
        if (tile && tile->type == TileType::Depot) {
            const auto o = targetOrder(v);
            if (o && o->kind == OrderKind::Depot && DepotId(tile->index) == o->depot) {
                enterDepot(w, id, o->depot);
                return;
            }
            if (DepotId(tile->index) == v.depot) {
                // Used the depot to turn around.
                v.exit = v.entry;
                continue;
            }
        }
        for (usize guardOrder = 0; guardOrder < v.orders.size(); ++guardOrder) {
            const auto o = targetOrder(v);
            if (!o || !o->passThrough()) break;
            const bool reached = o->kind == OrderKind::Via ? v.tile == o->waypoint
                : tile && tile->type == TileType::Station && tile->index == o->station.value;
            if (!reached) break;
            advanceOrder(v);
        }
        v.exit = chooseExit(w, id);
        v.stopAtMiddle = isStopTarget(w, v, v.tile) && turnKind(v.entry, v.exit) == TurnKind::Straight;
    }
}

u16 freeUnitNumber(const World& w, CompanyId owner, VehicleType type) {
    std::vector<u16> used;
    for (const auto [id, v] : w.vehicles)
        if (v.owner == owner && v.info().type == type) used.push_back(v.unitNumber);
    std::sort(used.begin(), used.end());
    u16 n = 1;
    for (const u16 u : used) {
        if (u == n) ++n;
        else if (u > n) break;
    }
    return n;
}

} // namespace

VehicleId buy(World& w, CompanyId owner, DepotId depotId, u16 model) {
    const Depot& d = w.depots[depotId];
    const VehicleModel& m = vehicleModel(model);
    const u16 unit = freeUnitNumber(w, owner, m.type);
    const VehicleId id = w.vehicles.create();
    Vehicle& v = w.vehicles[id];
    v.serial = ++w.nextSerial;
    v.owner = owner;
    v.model = model;
    v.unitNumber = unit;
    v.state = VehicleState::InDepot;
    v.stopped = true;
    v.depot = depotId;
    v.tile = d.tile;
    v.entry = d.facing;
    v.exit = opposite(d.facing);
    v.value = w.inflated(m.price);
    v.builtTick = w.clock.tick;
    v.reliability = maxReliability(m);
    v.lastServiceTick = w.clock.tick;
    if (m.type == VehicleType::Rail) v.cars = {model};
    return id;
}

void sell(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    dropAll(w, v);
    if (v.info().type == VehicleType::Rail) trains::releaseAll(w, id);
    w.vehicles.destroy(id);
}

void start(World& w, VehicleId id) { w.vehicles[id].stopped = false; }

void dropPassengers(World& w, VehicleId id) { dropAll(w, w.vehicles[id]); }

void invalidatePaths(World& w) {
    for (auto [id, v] : w.vehicles) {
        v.path.clear();
        v.pathIndex = 0;
    }
}

void tickAll(World& w) {
    w.vehicleTiles.clear();
    for (const auto [id, v] : w.vehicles)
        if (v.state != VehicleState::InDepot) w.vehicleTiles[tileKey(v.tile)].push_back(id);
    for (auto [id, v] : w.vehicles) {
        switch (v.state) {
        case VehicleState::InDepot:
            if (!v.stopped && !v.orders.empty()) {
                if (v.info().type == VehicleType::Rail)
                    trains::leaveDepot(w, id);
                else if (v.info().type == VehicleType::Ship)
                    ships::leaveDepot(w, id);
                else
                    leaveDepot(w, id);
            }
            break;
        case VehicleState::Loading:
            if (w.clock.tick >= v.loadingUntil) finishLoading(w, id);
            break;
        case VehicleState::Driving:
            if (v.stopped) {
                v.speed = 0;
                break;
            }
            if (v.breakdownTicks > 0) {
                --v.breakdownTicks;
                v.speed = 0;
                break;
            }
            if (v.info().type == VehicleType::Rail)
                trains::tick(w, id);
            else if (v.info().type == VehicleType::Ship)
                ships::tick(w, id);
            else
                drive(w, id);
            break;
        }
    }
}

void arriveInDepot(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const VehicleModel& m = v.info();
    // Old vehicles never quite get back to new condition.
    const bool old = ageDays(w, v) > m.lifeYears * 365;
    v.reliability = static_cast<u16>(maxReliability(m) - (old ? 1500 : 0));
    v.lastServiceTick = w.clock.tick;
    v.breakdownsSinceService = 0;
    v.breakdownTicks = 0;
    if (v.sendToDepot) {
        v.sendToDepot = false;
        v.goingForService = false;
        v.stopped = true;
    } else if (v.goingForService) {
        v.goingForService = false;
    } else if (!v.orders.empty()) {
        v.orderIndex = static_cast<u8>((v.orderIndex + 1) % v.orders.size());
    }
}

i32 ageDays(const World& w, const Vehicle& v) { return static_cast<i32>((w.clock.tick - v.builtTick) / kTicksPerDay); }

void daily(World& w) {
    constexpr i32 kBreakdownTicks = 2 * kTicksPerHour;
    for (auto [id, v] : w.vehicles) {
        if (v.state == VehicleState::InDepot && v.stopped) continue;
        const Money cost = (w.inflated(v.runningCost()) + 182) / 365;
        if (Company* c = w.companies.tryGet(v.owner)) c->spend(runningCategory(v.info().type), cost);
        v.profitThisYear -= cost;
        if (v.state == VehicleState::InDepot) continue;

        // Wear: reliability drops every day, twice as fast once the vehicle is past its life span.
        const VehicleModel& m = v.info();
        const i32 age = ageDays(w, v);
        const bool old = age > m.lifeYears * 365;
        v.reliability = static_cast<u16>(std::max(0, v.reliability - (old ? 36 : 18)));
        if (age == m.lifeYears * 365)
            w.addNews(NewsKind::Vehicle,
                      std::format("{} {} is getting old and should be replaced", vehicleTypeName(m.type), v.unitNumber),
                      v.tile);

        // Breakdowns: rare while well kept, frequent when neglected.
        if (v.breakdownTicks == 0 && v.state == VehicleState::Driving && !v.stopped && v.reliability < 9000) {
            const u32 gap = static_cast<u32>(9000 - v.reliability);
            if (w.rng.below(100'000) < gap * gap / 15'000) {
                v.breakdownTicks = kBreakdownTicks;
                if (v.breakdownsSinceService < 255) ++v.breakdownsSinceService;
            }
        }

        // Service visits.
        if (v.goingForService && v.lost) {
            // Can't reach the depot: give up for now and try again later.
            v.goingForService = false;
            v.lost = false;
            v.lastServiceTick = w.clock.tick - serviceIntervalDays(m.type) * kTicksPerDay / 2;
        }
        const i64 sinceService = (w.clock.tick - v.lastServiceTick) / kTicksPerDay;
        const Order* cur = v.currentOrder();
        if (!v.goingForService && !v.sendToDepot && v.state == VehicleState::Driving &&
            sinceService >= serviceIntervalDays(m.type) && w.depots.contains(v.depot) &&
            !(cur && cur->kind == OrderKind::Depot)) {
            v.goingForService = true;
            v.path.clear();
            v.pathIndex = 0;
        }
    }
}

void yearly(World& w) {
    for (auto [id, v] : w.vehicles) {
        v.profitLastYear = v.profitThisYear;
        v.profitThisYear = 0;
        v.passengersThisYear = 0;
        const VehicleModel& m = v.info();
        v.value = std::max<Money>(0, v.value - m.price / std::max(1, m.lifeYears));
    }
}

} // namespace tp::vehicles
