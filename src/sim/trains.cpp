// Train movement: path reservation through path signals, consists following the head's trail,
// stopping at platform ends, reversing at termini and depots.
//
// A train only ever moves onto track it has reserved. It reserves ahead in blocks: from its
// reservation frontier along the path to its destination up to (but excluding) the next tile
// with a signal facing it, or to the far end of the target platform. If any piece of a block is
// reserved by another train the block is not taken and the train waits in front of the signal.

#include "core/math.hpp"
#include "sim/passengers.hpp"
#include "sim/rail.hpp"
#include "sim/rail_path.hpp"
#include "sim/vehicle.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <optional>

namespace tp::trains {

namespace {

constexpr i32 kDecel = 3;      // braking, units per tick per tick
constexpr i32 kStopMargin = 6; // stop this far before the end of the reservation
constexpr i64 kBaseDwellTicks = 4 * kTicksPerMinute;

enum class Extend { Extended, AtTarget, Blocked, NoPath, NoOrders };

u8 segBit(const TrackSeg& s) { return s.entry == s.exit ? kTrackNS : trackBetween(s.entry, s.exit); }
i32 segLen(const TrackSeg& s) { return trackLength(segBit(s)); }

DepotId allowedDepot(const Vehicle& v) {
    const auto o = targetOrder(v);
    return o && o->kind == OrderKind::Depot ? o->depot : DepotId{};
}

bool isPlatformOf(const World& w, TilePos t, StationId s) {
    const Tile* tile = w.map.tile(t);
    return tile && tile->type == TileType::Station && static_cast<StopKind>(tile->extra) == StopKind::Rail &&
           tile->index == s.value;
}

std::vector<TilePos> goalsFor(const World& w, const Order& o) {
    std::vector<TilePos> goals;
    if (o.kind == OrderKind::Via) {
        goals.push_back(o.waypoint);
    } else if (o.kind == OrderKind::Station) {
        if (const Station* s = w.stations.tryGet(o.station))
            for (const TilePos t : s->tiles)
                if (isPlatformOf(w, t, o.station)) goals.push_back(t);
    } else if (const Depot* d = w.depots.tryGet(o.depot); d && d->kind == DepotKind::Rail) {
        goals.push_back(d->tile);
    }
    return goals;
}

bool reservable(const World& w, TilePos t, u8 bit) {
    const Tile* tile = w.map.tile(t);
    if (!tile) return false;
    if (tile->type == TileType::Road && tile->rail && crossingRoadOccupied(w, t)) return false;
    const u8 r = tile->extra2 & kTrackAll;
    for (u8 b = 1; b <= kTrackNW; b = static_cast<u8>(b << 1))
        if ((r & b) && tracksConflict(b, bit)) return false;
    return true;
}

void reserve(World& w, const TrackSeg& s) {
    if (Tile* t = w.map.tile(s.tile)) t->extra2 = static_cast<u8>(t->extra2 | segBit(s));
}

void unreserve(World& w, const TrackSeg& s) {
    if (Tile* t = w.map.tile(s.tile)) t->extra2 = static_cast<u8>(t->extra2 & ~segBit(s));
}

void syncHead(Vehicle& v) {
    if (v.trail.empty()) return;
    v.tile = v.trail.front().tile;
    v.entry = v.trail.front().entry;
    v.exit = v.trail.front().exit;
}

i32 reservedDistance(const Vehicle& v) {
    if (v.trail.empty()) return 0;
    i32 d = segLen(v.trail.front()) - v.progress;
    for (const TrackSeg& s : v.ahead) d += segLen(s);
    return d;
}

i32 acceleration(const Vehicle& v) {
    i32 powered = 0;
    for (const u16 m : v.cars) powered += vehicleModel(m).powered ? 1 : 0;
    const i32 cars = std::max<i32>(1, static_cast<i32>(v.cars.size()));
    return std::max(1, 4 * std::max(1, powered) / cars);
}

// Reserves the next block towards the current destination.
Extend extend(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const auto o = targetOrder(v);
    if (!o) return Extend::NoOrders;
    const TrackSeg last = v.ahead.empty() ? v.trail.front() : v.ahead.back();
    if (o->kind == OrderKind::Via && last.tile == o->waypoint) return Extend::AtTarget;
    // The reservation already ends at the far end of the target platform (or in the depot).
    if (o->kind == OrderKind::Station && isPlatformOf(w, last.tile, o->station) &&
        !isPlatformOf(w, neighbour(last.tile, last.exit), o->station))
        return Extend::AtTarget;
    if (const Tile* lt = w.map.tile(last.tile); o->kind == OrderKind::Depot && lt && lt->type == TileType::Depot &&
                                                DepotId(lt->index) == o->depot)
        return Extend::AtTarget;
    if (!railLinkOpen(w, last.tile, last.exit)) return Extend::NoPath;
    const TilePos start = neighbour(last.tile, last.exit);
    const Dir entry = opposite(last.exit);
    const std::vector<TilePos> goals = goalsFor(w, *o);
    if (goals.empty()) return Extend::NoPath;
    if (const Tile* tile = w.map.tile(start);
        tile && tile->type == TileType::Rail && (tile->extra & kSignalBlock) &&
        (tile->extra & signalFacing(tile->rail, opposite(entry))) &&
        !blockSignalClear(w, start, opposite(entry), id)) return Extend::Blocked;
    const auto path = findRailPath(w, start, entry, goals, allowedDepot(v));
    if (!path) return Extend::NoPath;

    std::vector<TrackSeg> collected;
    TilePos t = start;
    Dir e = entry;
    for (usize i = 0;; ++i) {
        const Tile* tile = w.map.tile(t);
        if (!tile) break;
        const bool goal = std::find(goals.begin(), goals.end(), t) != goals.end();
        if (goal && tile->type == TileType::Depot) {
            const TrackSeg s{t, e, opposite(e)};
            if (!reservable(w, t, segBit(s))) return Extend::Blocked;
            collected.push_back(s);
            break;
        }
        if (goal && o->kind == OrderKind::Via) {
            // Choose the waypoint's exit towards the following order, including curves/junctions.
            const u8 exits = railExits(w, t, e, allowedDepot(v));
            Dir x = opposite(e);
            if (v.orders.size() > 1) {
                const Order& following = v.orders[(v.orderIndex + 1) % v.orders.size()];
                const auto onward = findRailPath(w, t, e, goalsFor(w, following),
                    following.kind == OrderKind::Depot ? following.depot : DepotId{});
                if (onward && !onward->empty()) x = static_cast<Dir>((*onward)[0] & 3);
            }
            if (!(exits & dirBit(x))) {
                bool found = false;
                for (const Dir d : kDirs) if (exits & dirBit(d)) { x = d; found = true; break; }
                if (!found) return Extend::NoPath;
            }
            const TrackSeg s{t, e, x};
            if (!reservable(w, t, segBit(s))) return Extend::Blocked;
            collected.push_back(s);
            break;
        }
        if (goal) {
            // Run through to the far end of the platform.
            const Dir x = opposite(e);
            TilePos p = t;
            for (int guard = 0; guard < 64; ++guard) {
                const TrackSeg s{p, opposite(x), x};
                if (!reservable(w, p, segBit(s))) return Extend::Blocked;
                collected.push_back(s);
                const TilePos n = neighbour(p, x);
                if (!isPlatformOf(w, n, o->station) || !railLinkOpen(w, p, x)) break;
                p = n;
            }
            break;
        }
        if (i >= path->size()) break;
        const Dir x = static_cast<Dir>((*path)[i] & 3);
        const u8 bit = trackBetween(e, x);
        if (!bit) break;
        // A signal facing us starts the next block.
        if (i > 0 && tile->type == TileType::Rail && isStraightTrack(bit) && (tile->extra & signalFacing(bit, x)))
            break;
        if (!reservable(w, t, bit)) return Extend::Blocked;
        collected.push_back({t, e, x});
        t = neighbour(t, x);
        e = opposite(x);
    }
    if (collected.empty()) return Extend::Blocked;
    for (const TrackSeg& s : collected) {
        reserve(w, s);
        v.ahead.push_back(s);
    }
    v.lost = false;
    return Extend::Extended;
}

void trimTrail(World& w, Vehicle& v) {
    const i32 len = consistLength(v);
    i32 covered = v.progress;
    usize keep = 1;
    while (keep < v.trail.size() && covered < len) {
        covered += segLen(v.trail[keep]);
        ++keep;
    }
    while (v.trail.size() > keep) {
        unreserve(w, v.trail.back());
        v.trail.pop_back();
    }
}

// True when the whole consist is out on the track (not partly inside a depot).
bool fullyOnTrack(const Vehicle& v) {
    i32 covered = v.progress;
    for (usize i = 1; i < v.trail.size(); ++i) covered += segLen(v.trail[i]);
    return covered >= consistLength(v);
}

void reverse(World& w, Vehicle& v) {
    for (const TrackSeg& s : v.ahead) unreserve(w, s);
    v.ahead.clear();
    // Find the tail: segment j at progress q (measured from that segment's entry).
    i32 remaining = consistLength(v);
    usize j = 0;
    i32 q = 0;
    if (remaining <= v.progress) {
        q = v.progress - remaining;
    } else {
        remaining -= v.progress;
        j = 1;
        while (j < v.trail.size()) {
            const i32 l = segLen(v.trail[j]);
            if (remaining <= l) {
                q = l - remaining;
                break;
            }
            remaining -= l;
            ++j;
        }
        if (j >= v.trail.size()) {
            j = v.trail.size() - 1;
            q = 0;
        }
    }
    std::deque<TrackSeg> flipped;
    for (usize i = j + 1; i-- > 0;) flipped.push_back({v.trail[i].tile, v.trail[i].exit, v.trail[i].entry});
    for (usize i = j + 1; i < v.trail.size(); ++i) unreserve(w, v.trail[i]);
    v.progress = segLen(flipped.front()) - q;
    v.trail = std::move(flipped);
    v.speed = 0;
    syncHead(v);
}

void enterDepot(World& w, VehicleId id, DepotId did) {
    Vehicle& v = w.vehicles[id];
    releaseAll(w, id);
    for (const PersonId p : v.passengers) passengers::abandonRide(w, p);
    v.passengers.clear();
    v.state = VehicleState::InDepot;
    v.speed = 0;
    v.progress = 0;
    v.depot = did;
    if (const Depot* d = w.depots.tryGet(did)) {
        v.tile = d->tile;
        v.entry = d->facing;
        v.exit = opposite(d->facing);
    }
    vehicles::arriveInDepot(w, id);
}

void arriveAtPlatform(World& w, VehicleId id, StationId sid) {
    Vehicle& v = w.vehicles[id];
    v.state = VehicleState::Loading;
    v.speed = 0;
    const i32 exchanged = passengers::exchangeAtStop(w, id, sid);
    v.loadingUntil = w.clock.tick + kBaseDwellTicks + exchanged / 3;
    stations::recordVisit(w, sid, id);
}

void advance(World& w, VehicleId id, i32 move) {
    Vehicle& v = w.vehicles[id];
    v.progress += move;
    while (v.progress >= segLen(v.trail.front())) {
        if (v.ahead.empty()) {
            v.progress = segLen(v.trail.front());
            v.speed = 0;
            break;
        }
        v.progress -= segLen(v.trail.front());
        const TrackSeg next = v.ahead.front();
        v.ahead.pop_front();
        v.trail.push_front(next);
        const Tile* t = w.map.tile(next.tile);
        for (usize guard = 0; guard < v.orders.size(); ++guard) {
            const auto o = targetOrder(v);
            if (!o || !o->passThrough()) break;
            const bool reached = o->kind == OrderKind::Via ? next.tile == o->waypoint
                : isPlatformOf(w, next.tile, o->station);
            if (!reached) break;
            v.orderIndex = static_cast<u8>((v.orderIndex + 1) % v.orders.size());
            v.retryTicks = 0;
        }
        if (t && t->type == TileType::Depot) {
            const auto o = targetOrder(v);
            if (o && o->kind == OrderKind::Depot && DepotId(t->index) == o->depot) {
                enterDepot(w, id, o->depot);
                return;
            }
        }
    }
    trimTrail(w, v);
    syncHead(v);
}

} // namespace

i32 consistLength(const Vehicle& v) {
    if (v.cars.empty()) return v.info().length * 16;
    i32 len = 0;
    for (const u16 m : v.cars) len += vehicleModel(m).length * 16 + 4;
    return len;
}

i32 maxSpeed(const Vehicle& v) {
    i32 kmh = v.info().maxSpeedKmh;
    for (const u16 m : v.cars) kmh = std::min<i32>(kmh, vehicleModel(m).maxSpeedKmh);
    return std::max(1, speedUnits(kmh));
}

i32 trackSpeedLimit(const Vehicle& v) {
    i32 limit = maxSpeed(v);
    const i32 curve = speedUnits(60);
    if (v.trail.empty() || limit <= curve) return limit;
    // Keep the restriction until the last carriage has cleared the curve.
    for (const TrackSeg& s : v.trail)
        if (!isStraightTrack(segBit(s))) return curve;
    i64 distance = std::max(0, segLen(v.trail.front()) - v.progress);
    for (const TrackSeg& s : v.ahead) {
        if (!isStraightTrack(segBit(s))) {
            const u64 safeSquared = static_cast<u64>(curve * curve + 2 * kDecel * distance);
            limit = std::min(limit, static_cast<i32>(isqrt(safeSquared)));
        }
        distance += segLen(s);
        if (2 * kDecel * distance >= static_cast<i64>(limit) * limit) break;
    }
    return limit;
}

void releaseAll(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    for (const TrackSeg& s : v.trail) unreserve(w, s);
    for (const TrackSeg& s : v.ahead) unreserve(w, s);
    v.trail.clear();
    v.ahead.clear();
}

bool leaveDepot(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    if (v.retryTicks > 0) {
        --v.retryTicks;
        return false;
    }
    const Depot* d = w.depots.tryGet(v.depot);
    if (!d || d->kind != DepotKind::Rail || v.orders.empty()) return false;
    const TrackSeg seg{d->tile, opposite(d->facing), d->facing};
    if (!reservable(w, d->tile, segBit(seg))) {
        v.retryTicks = 15;
        return false;
    }
    v.trail.clear();
    v.ahead.clear();
    v.trail.push_back(seg);
    v.progress = segLen(seg);
    reserve(w, seg);
    if (extend(w, id) != Extend::Extended) {
        unreserve(w, seg);
        v.trail.clear();
        v.retryTicks = 30;
        return false;
    }
    v.state = VehicleState::Driving;
    v.speed = 0;
    v.blockedTicks = 0;
    syncHead(v);
    return true;
}

void tick(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    if (v.trail.empty()) {
        // Should not happen; put the train back into its depot rather than crash.
        v.state = VehicleState::InDepot;
        return;
    }
    const i32 vmax = maxSpeed(v);
    if (v.retryTicks > 0) --v.retryTicks;
    const i32 braking = (v.speed * v.speed) / (2 * kDecel) + 2 * kTileUnits;
    if (reservedDistance(v) < braking && v.retryTicks == 0) {
        const Extend r = extend(w, id);
        if (r == Extend::Blocked || r == Extend::NoPath || r == Extend::NoOrders)
            v.retryTicks = r == Extend::Blocked ? 8 : 16;
        if (r == Extend::Extended) v.blockedTicks = 0;
        // Stopped at the end of the reservation with no way forward: turn around (once).
        if (r == Extend::NoPath && v.speed == 0 && reservedDistance(v) <= kStopMargin + 2 && fullyOnTrack(v)) {
            if (v.blockedTicks == 0) {
                reverse(w, v);
                v.blockedTicks = 1;
                v.retryTicks = 0;
                return;
            }
            v.lost = true;
            v.blockedTicks = 0;
            v.retryTicks = 64;
        }
    }

    const i32 dist = std::max(0, reservedDistance(v) - kStopMargin);
    i32 desired = std::min<i32>(vmax, static_cast<i32>(isqrt(static_cast<u64>(2 * kDecel * dist))));
    desired = std::min(desired, trackSpeedLimit(v));
    if (const auto up = w.map.slope(v.tile).rampUphill(); up && *up == v.exit && desired > vmax * 3 / 4 &&
        (!w.map.tile(v.tile) || !w.map.tile(v.tile)->span))
        desired = vmax * 3 / 4; // climbing
    if (v.speed < desired) v.speed = std::min(desired, v.speed + acceleration(v));
    else v.speed = std::max(desired, v.speed - kDecel * 2);
    const i32 move = std::min(v.speed, std::max(0, reservedDistance(v)));
    if (move > 0) advance(w, id, move);
    if (v.state != VehicleState::Driving) return;

    // Arrived at the end of the target platform?
    if (v.ahead.empty() && segLen(v.trail.front()) - v.progress <= kStopMargin + 2) {
        const auto o = targetOrder(v);
        if (o && o->kind == OrderKind::Station && !o->passThrough() && isPlatformOf(w, v.tile, o->station)) {
            v.speed = 0;
            arriveAtPlatform(w, id, o->station);
        }
    }
}

} // namespace tp::trains
