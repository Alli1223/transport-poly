// Trains on free-form track (see track.hpp): the same rules as on the old grid railway, over the
// node / edge graph instead of tile pieces.
//
// A train only moves onto edges it holds. It reserves ahead block by block: from the end of its
// reservation along its route up to (not past) the next node with a signal facing it, or to the
// far end of the target platform. A block is taken whole or not at all, so trains wait at signals
// for each other. Behind the train, edges are released once its last car has left them.

#include "core/math.hpp"
#include "sim/passengers.hpp"
#include "sim/track.hpp"
#include "sim/vehicle.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <optional>
#include <queue>
#include <unordered_map>

namespace tp::tracktrains {

namespace {

constexpr i32 kDecel = 3;      // braking, units per tick per tick
constexpr i32 kStopMargin = 6; // stop this far before the end of the reservation
constexpr i64 kBaseDwellTicks = 4 * kTicksPerMinute;

enum class Extend { Extended, AtTarget, Blocked, NoPath, NoOrders };

i32 runLen(const World& w, EdgeRun r) { return w.trackEdges[r.edge].length; }

u64 runKey(EdgeRun r) { return (static_cast<u64>(r.edge.value) << 1) | (r.forward ? 1u : 0u); }

bool isPlatformOf(const World& w, TrackEdgeId e, StationId s) {
    const TrackEdge* x = w.trackEdges.tryGet(e);
    return x && x->kind == TrackKind::Platform && x->station == s;
}

// Driving into the depot building (towards the stub's dead end).
bool intoDepot(const World& w, EdgeRun r, DepotId d) {
    const TrackEdge& e = w.trackEdges[r.edge];
    return e.kind == TrackKind::Depot && e.depot == d && r.forward;
}

// The side of the node a run leaves through (at its start).
u8 runStartSide(const World& w, EdgeRun r) {
    const TrackEdge& e = w.trackEdges[r.edge];
    return r.forward ? e.sideA : e.sideB;
}

// Where an order sends a train: a chosen platform (its edges), any platform of the station, the
// way into a depot, or track over a waypoint tile.
struct Goal {
    Order order;
    std::vector<TrackEdgeId> platform;

    bool operator()(const World& w, EdgeRun r) const {
        switch (order.kind) {
        case OrderKind::Station:
            return platform.empty() ? isPlatformOf(w, r.edge, order.station) : std::ranges::find(platform, r.edge) != platform.end();
        case OrderKind::Depot: return intoDepot(w, r, order.depot);
        case OrderKind::Via:
            return w.trackEdges[r.edge].kind != TrackKind::Depot && track::edgeCovers(w, r.edge, order.waypoint);
        }
        return false;
    }
};

Goal goalFor(const World& w, const Order& o) {
    Goal g{o, {}};
    if (o.kind == OrderKind::Station && (o.flags & kOrderPlatform))
        if (const TrackEdgeId e = track::platformNear(w, o.station, o.platform); e.valid()) g.platform = track::platformChain(w, e);
    return g;
}

// Passing a waypoint or a non-stop station counts as visiting it: on to the next order.
void passOrders(const World& w, Vehicle& v, EdgeRun r) {
    for (usize guard = 0; guard < v.orders.size(); ++guard) {
        const auto o = targetOrder(v);
        if (!o || !o->passThrough() || !goalFor(w, *o)(w, r)) break;
        v.orderIndex = static_cast<u8>((v.orderIndex + 1) % v.orders.size());
        v.retryTicks = 0;
    }
}

// One-way signals cannot be passed from behind.
bool passable(const TrackNode& n, u8 leaveSide) { return n.signals != (1u << (1 - leaveSide)); }

void syncHead(const World& w, Vehicle& v) {
    if (v.runs.empty()) return;
    const TrackSample p = track::pointOn(w.trackEdges[v.runs.front().edge], v.runs.front().forward, v.progress);
    v.tile = tileAt(p.p);
}

i32 reservedDistance(const World& w, const Vehicle& v) {
    if (v.runs.empty()) return 0;
    i32 d = runLen(w, v.runs.front()) - v.progress;
    for (const EdgeRun r : v.runsAhead) d += runLen(w, r);
    return d;
}

// The grade under a train in fine height units per tile (positive climbing): its head's height
// against its tail's, so it feels a hill more as more of it runs onto the slope.
i32 gradeUnder(const World& w, const Vehicle& v) {
    const i32 head = track::pointOn(w.trackEdges[v.runs.front().edge], v.runs.front().forward, v.progress).h;
    i32 span = std::max(kTrackUnits, trains::consistLength(v));
    i32 back = span;
    usize j = 0;
    i32 at = v.progress;
    while (back > at) {
        if (j + 1 >= v.runs.size()) {
            span -= back - at;
            at = 0;
            break;
        }
        back -= at;
        ++j;
        at = runLen(w, v.runs[j]);
    }
    if (back <= at) at -= back;
    const i32 tail = track::pointOn(w.trackEdges[v.runs[j].edge], v.runs[j].forward, at).h;
    return span > 0 ? (head - tail) * kTrackUnits / span : 0;
}

// Speed limit for a curve of radius r (units): sharper curves are slower.
i32 curveLimit(i32 r) { return speedUnits(30 + r * 12 / kTrackUnits) * trains::kRailSpeedPercent / 100; }

template <class GoalFn>
std::optional<std::vector<EdgeRun>> findPath(const World& w, EdgeRun from, GoalFn&& goal, DepotId allowDepot, VehicleId train) {
    struct Item {
        i64 cost;
        u32 order;
        EdgeRun run;
        bool operator<(const Item& o) const { return cost != o.cost ? cost > o.cost : order > o.order; }
    };
    std::priority_queue<Item> open;
    std::unordered_map<u64, std::pair<u64, i64>> parent; // run -> (parent run key, cost); root parent ~0
    u32 order = 0;
    // Electric trains keep to track with overhead wires.
    const bool wires = w.vehicles.contains(train) && w.vehicles[train].needsWires();
    const auto push = [&](EdgeRun r, u64 from, i64 cost) {
        const TrackEdge& e = w.trackEdges[r.edge];
        if (wires && !e.electrified) return;
        // Never route through a depot other than the one we are heading for.
        if (e.kind == TrackKind::Depot && !(e.depot == allowDepot && r.forward)) return;
        // Track another train holds counts as longer, so a free platform wins over a busy one.
        if (e.reservedBy.valid() && e.reservedBy != train) cost += 16 * kTrackUnits;
        const auto [it, inserted] = parent.try_emplace(runKey(r), from, cost);
        if (!inserted) {
            if (it->second.second <= cost) return;
            it->second = {from, cost};
        }
        open.push({cost, order++, r});
    };
    const TrackNode& start = w.trackNodes[track::runEnd(w, from)];
    for (const EdgeRun c : track::continuations(w, from))
        if (passable(start, runStartSide(w, c))) push(c, ~0ull, runLen(w, c));
    usize expanded = 0;
    while (!open.empty()) {
        const Item it = open.top();
        open.pop();
        if (parent[runKey(it.run)].second < it.cost) continue;
        if (goal(it.run)) {
            std::vector<EdgeRun> path;
            u64 k = runKey(it.run);
            while (k != ~0ull) {
                path.push_back({TrackEdgeId{static_cast<u32>(k >> 1)}, (k & 1) != 0});
                k = parent[k].first;
            }
            std::ranges::reverse(path);
            return path;
        }
        if (++expanded > 20000) break;
        const TrackNode& n = w.trackNodes[track::runEnd(w, it.run)];
        for (const EdgeRun c : track::continuations(w, it.run))
            if (passable(n, runStartSide(w, c))) push(c, runKey(it.run), it.cost + runLen(w, c));
    }
    return std::nullopt;
}

Extend extend(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    passOrders(w, v, v.runs.front()); // standing on a waypoint already
    std::optional<Order> o = targetOrder(v);
    if (!o) return Extend::NoOrders;
    const EdgeRun last = v.runsAhead.empty() ? v.runs.front() : v.runsAhead.back();
    // Plan for the first order the reservation does not already reach: waypoints and non-stop
    // stations reserved through are as good as passed.
    if (!v.sendToDepot && !v.goingForService) {
        usize k = v.orderIndex % v.orders.size();
        for (usize guard = 0; guard + 1 < v.orders.size() && o->passThrough(); ++guard) {
            const Goal g = goalFor(w, *o);
            if (std::ranges::none_of(v.runsAhead, [&](EdgeRun r) { return g(w, r); })) break;
            k = (k + 1) % v.orders.size();
            o = v.orders[k];
        }
    }
    const Goal goal = goalFor(w, *o);
    // Already reserved to the far end of the target platform, or into the depot.
    if (o->kind == OrderKind::Station && !o->passThrough() && goal(w, last)) {
        bool more = false;
        for (const EdgeRun c : track::continuations(w, last)) more = more || goal(w, c);
        if (!more) return Extend::AtTarget;
    }
    if (o->kind == OrderKind::Depot && goal(w, last)) return Extend::AtTarget;
    const auto reached = [&](EdgeRun r) { return goal(w, r); };
    const auto path = findPath(w, last, reached, o->kind == OrderKind::Depot ? o->depot : DepotId{}, id);
    if (!path) return Extend::NoPath;

    std::vector<EdgeRun> collected;
    for (usize i = 0; i < path->size(); ++i) {
        const EdgeRun r = (*path)[i];
        // A signal facing us at the start of this run begins the next block.
        if (i > 0) {
            const TrackNode& n = w.trackNodes[track::runStart(w, r)];
            if (n.signals & (1u << runStartSide(w, r))) break;
        }
        if (!track::reservable(w, r.edge, id)) return Extend::Blocked;
        collected.push_back(r);
        if (goal(w, r)) {
            if (o->kind == OrderKind::Station && !o->passThrough()) {
                // Run through to the far end of the platform.
                EdgeRun cur = r;
                for (int guard = 0; guard < 16; ++guard) {
                    std::optional<EdgeRun> next;
                    for (const EdgeRun c : track::continuations(w, cur))
                        if (goal(w, c)) next = c;
                    if (!next) break;
                    if (!track::reservable(w, next->edge, id)) return Extend::Blocked;
                    collected.push_back(*next);
                    cur = *next;
                }
            }
            break;
        }
    }
    if (collected.empty()) return Extend::Blocked;
    for (const EdgeRun r : collected) {
        w.trackEdges[r.edge].reservedBy = id;
        v.runsAhead.push_back(r);
    }
    v.lost = false;
    return Extend::Extended;
}

void releaseEdge(World& w, const Vehicle& v, VehicleId id, TrackEdgeId e) {
    // Still in use further along the train (loops) or ahead: keep it.
    for (const EdgeRun r : v.runs)
        if (r.edge == e) return;
    for (const EdgeRun r : v.runsAhead)
        if (r.edge == e) return;
    TrackEdge& x = w.trackEdges[e];
    if (x.reservedBy == id) x.reservedBy = {};
}

void trimTrail(World& w, Vehicle& v, VehicleId id) {
    const i32 len = trains::consistLength(v);
    i32 covered = v.progress;
    usize keep = 1;
    while (keep < v.runs.size() && covered < len) {
        covered += runLen(w, v.runs[keep]);
        ++keep;
    }
    while (v.runs.size() > keep) {
        const TrackEdgeId e = v.runs.back().edge;
        v.runs.pop_back();
        releaseEdge(w, v, id, e);
    }
}

bool fullyOnTrack(const World& w, const Vehicle& v) {
    i32 covered = v.progress;
    for (usize i = 1; i < v.runs.size(); ++i) covered += runLen(w, v.runs[i]);
    return covered >= trains::consistLength(v);
}

void reverse(World& w, Vehicle& v, VehicleId id) {
    const std::deque<EdgeRun> ahead = v.runsAhead;
    v.runsAhead.clear();
    for (const EdgeRun r : ahead) releaseEdge(w, v, id, r.edge);
    // Find the tail: run j at distance q from that run's start.
    i32 remaining = trains::consistLength(v);
    usize j = 0;
    i32 q = 0;
    if (remaining <= v.progress) {
        q = v.progress - remaining;
    } else {
        remaining -= v.progress;
        j = 1;
        while (j < v.runs.size()) {
            const i32 l = runLen(w, v.runs[j]);
            if (remaining <= l) {
                q = l - remaining;
                break;
            }
            remaining -= l;
            ++j;
        }
        if (j >= v.runs.size()) {
            j = v.runs.size() - 1;
            q = 0;
        }
    }
    std::deque<EdgeRun> flipped;
    for (usize i = j + 1; i-- > 0;) flipped.push_back({v.runs[i].edge, !v.runs[i].forward});
    const std::deque<EdgeRun> dropped(v.runs.begin() + static_cast<std::ptrdiff_t>(j + 1), v.runs.end());
    v.runs = std::move(flipped);
    for (const EdgeRun r : dropped) releaseEdge(w, v, id, r.edge);
    v.progress = runLen(w, v.runs.front()) - q;
    v.speed = 0;
    syncHead(w, v);
}

void enterDepot(World& w, VehicleId id, DepotId did) {
    Vehicle& v = w.vehicles[id];
    releaseAll(w, id);
    for (const PersonId p : v.passengers) passengers::abandonRide(w, p);
    v.passengers.clear();
    v.passengerSeats.clear();
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
    passengers::exchangeAtStop(w, id, sid);
    v.loadingStartTick = w.clock.tick;
    v.loadingUntil = std::max(v.loadingUntil,w.clock.tick + kBaseDwellTicks + 8);
    stations::recordVisit(w, sid, id);
}

void advance(World& w, VehicleId id, i32 move) {
    Vehicle& v = w.vehicles[id];
    v.progress += move;
    while (v.progress >= runLen(w, v.runs.front())) {
        if (v.runsAhead.empty()) {
            v.progress = runLen(w, v.runs.front());
            v.speed = 0;
            break;
        }
        v.progress -= runLen(w, v.runs.front());
        const EdgeRun next = v.runsAhead.front();
        v.runsAhead.pop_front();
        v.runs.push_front(next);
        passOrders(w, v, next);
        if (const auto o = targetOrder(v); o && o->kind == OrderKind::Depot && intoDepot(w, next, o->depot)) {
            enterDepot(w, id, o->depot);
            return;
        }
    }
    trimTrail(w, v, id);
    syncHead(w, v);
}

// Speed limit from the curves under the train and those coming up (braking in time).
i32 speedLimit(const World& w, const Vehicle& v) {
    i32 limit = trains::maxSpeed(v);
    for (const EdgeRun r : v.runs) {
        const i32 rad = w.trackEdges[r.edge].minRadius;
        if (rad) limit = std::min(limit, curveLimit(rad));
    }
    i64 dist = std::max(0, runLen(w, v.runs.front()) - v.progress);
    for (const EdgeRun r : v.runsAhead) {
        const i32 rad = w.trackEdges[r.edge].minRadius;
        if (rad) {
            const i32 c = curveLimit(rad);
            limit = std::min(limit, static_cast<i32>(isqrt(static_cast<u64>(c) * c + static_cast<u64>(2 * kDecel * dist))));
        }
        dist += runLen(w, r);
        if (2 * kDecel * dist >= static_cast<i64>(limit) * limit) break;
    }
    return limit;
}

} // namespace

TrackEdgeId passengerPlatform(const World& w, VehicleId id, const Order& order) {
    const auto& v = w.vehicles[id];
    const bool current = &order == v.currentOrder();
    if (current && !v.runs.empty()) {
        if (isPlatformOf(w,v.runs.front().edge,order.station)) return v.runs.front().edge;
        for (const auto run : v.runsAhead)
            if (isPlatformOf(w,run.edge,order.station)) return run.edge;
    }
    if (order.flags & kOrderPlatform) return track::platformNear(w,order.station,order.platform);
    if (current && !v.runs.empty()) {
        const auto last = v.runsAhead.empty() ? v.runs.front() : v.runsAhead.back();
        const auto goal = goalFor(w,order);
        const auto path = findPath(w,last,[&](EdgeRun r){return goal(w,r);},DepotId{},id);
        if (path) for (const auto run : *path)
            if (isPlatformOf(w,run.edge,order.station)) return run.edge;
    }
    return {};
}

void releaseAll(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    const auto release = [&](TrackEdgeId e) {
        if (TrackEdge* x = w.trackEdges.tryGet(e); x && x->reservedBy == id) x->reservedBy = {};
    };
    for (const EdgeRun r : v.runs) release(r.edge);
    for (const EdgeRun r : v.runsAhead) release(r.edge);
    v.runs.clear();
    v.runsAhead.clear();
}

bool leaveDepot(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    if (v.retryTicks > 0) {
        --v.retryTicks;
        return false;
    }
    const Depot* d = w.depots.tryGet(v.depot);
    if (!d || !d->edge.valid() || v.orders.empty() || !w.trackEdges.contains(d->edge)) return false;
    const EdgeRun out{d->edge, false}; // from the building towards the track
    if (!track::reservable(w, d->edge, id)) {
        v.retryTicks = 15;
        return false;
    }
    v.runs.clear();
    v.runsAhead.clear();
    v.runs.push_back(out);
    v.progress = runLen(w, out);
    w.trackEdges[d->edge].reservedBy = id;
    if (extend(w, id) != Extend::Extended) {
        w.trackEdges[d->edge].reservedBy = {};
        v.runs.clear();
        v.retryTicks = 30;
        return false;
    }
    v.state = VehicleState::Driving;
    v.speed = 0;
    v.blockedTicks = 0;
    syncHead(w, v);
    return true;
}

void tick(World& w, VehicleId id) {
    Vehicle& v = w.vehicles[id];
    if (v.runs.empty() || !w.trackEdges.contains(v.runs.front().edge)) {
        // Should not happen; put the train back into its depot rather than crash.
        releaseAll(w, id);
        v.state = VehicleState::InDepot;
        v.stopped = true;
        return;
    }
    const i32 vmax = trains::maxSpeed(v);
    if (v.retryTicks > 0) --v.retryTicks;
    const i32 braking = (v.speed * v.speed) / (2 * kDecel) + 2 * kTrackUnits;
    if (reservedDistance(w, v) < braking && v.retryTicks == 0) {
        const Extend r = extend(w, id);
        if (r == Extend::Blocked || r == Extend::NoPath || r == Extend::NoOrders) v.retryTicks = r == Extend::Blocked ? 8 : 16;
        if (r == Extend::Extended) v.blockedTicks = 0;
        // Stopped at the end of the reservation with no way forward: turn around (once).
        if (r == Extend::NoPath && v.speed == 0 && reservedDistance(w, v) <= kStopMargin + 2 && fullyOnTrack(w, v)) {
            if (v.blockedTicks == 0) {
                reverse(w, v, id);
                v.blockedTicks = 1;
                v.retryTicks = 0;
                return;
            }
            v.lost = true;
            v.blockedTicks = 0;
            v.retryTicks = 64;
        }
    }

    const i32 dist = std::max(0, reservedDistance(w, v) - kStopMargin);
    i32 desired = std::min<i32>(vmax, static_cast<i32>(isqrt(static_cast<u64>(2 * kDecel * dist))));
    desired = std::min(desired, speedLimit(w, v));
    trains::accelerate(v, desired, gradeUnder(w, v));
    const i32 move = std::min(v.speed, std::max(0, reservedDistance(w, v)));
    if (move > 0) advance(w, id, move);
    if (v.state != VehicleState::Driving) return;

    // Arrived at the end of the target platform?
    if (v.runsAhead.empty() && runLen(w, v.runs.front()) - v.progress <= kStopMargin + 2) {
        const auto o = targetOrder(v);
        if (o && o->kind == OrderKind::Station && !o->passThrough() && goalFor(w, *o)(w, v.runs.front())) {
            v.speed = 0;
            arriveAtPlatform(w, id, o->station);
        }
    }
}

} // namespace tp::tracktrains
