#include "sim/passengers.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/feedback.hpp"
#include "sim/person.hpp"
#include "sim/rail.hpp"
#include "sim/subsidies.hpp"
#include "sim/vehicle.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <queue>
#include <tuple>

namespace tp {

const RouteEntry* RoutingTable::find(StationId from, StationId to) const {
    const auto it = routes.find(from.value);
    if (it == routes.end()) return nullptr;
    const auto jt = it->second.find(to.value);
    return jt == it->second.end() ? nullptr : &jt->second;
}

namespace passengers {

namespace {

constexpr i64 kWaitReviewTicks = 3 * kTicksPerHour;
constexpr i64 kDwellEstimate = 4 * kTicksPerMinute;

// The stations a vehicle stops at that take passengers (freight stations are passed by).
std::vector<StationId> stationOrders(const World& w, const Vehicle& v) {
    std::vector<StationId> list;
    for (const Order& o : v.orders) {
        if (o.kind != OrderKind::Station || o.passThrough() || !w.stations.contains(o.station)) continue;
        if (!w.stations[o.station].takesPassengers()) continue;
        if (!list.empty() && list.back() == o.station) continue;
        list.push_back(o.station);
    }
    if (list.size() > 1 && list.front() == list.back()) list.pop_back();
    return list;
}

// The next station the vehicle will stop at after the station of its current order.
StationId nextStopAfterCurrent(const World& w, const Vehicle& v) {
    const usize n = v.orders.size();
    for (usize k = 1; k <= n; ++k) {
        const Order& o = v.orders[(v.orderIndex + k) % n];
        if (o.kind == OrderKind::Station && !o.passThrough() && w.stations.contains(o.station)) return o.station;
    }
    return StationId{};
}

bool vehicleVisits(const Vehicle& v, StationId s) {
    for (const Order& o : v.orders)
        if (o.kind == OrderKind::Station && !o.passThrough() && o.station == s) return true;
    return false;
}

bool walkTransfer(World& w, PersonId pid, StationId from, StationId to) {
    const auto it = w.routing.transfers.find({from.value, to.value});
    if (it == w.routing.transfers.end() || it->second.empty()) return false;
    auto& p = w.people[pid];
    std::vector<WalkPoint> path;
    if (p.transitMotion == 2 && p.transitBegin == w.clock.tick) path = p.walkPath;
    const auto current = path.empty()
                             ? (p.walkPath.empty() ? it->second.front() : walking::position(p, w.clock.tick))
                             : path.back();
    auto internal = walking::withinStation(w, from, current.p, it->second.front().p);
    if (!internal.empty()) internal.front() = current;
    path.insert(path.end(), internal.begin(), internal.end());
    path.insert(path.end(), it->second.begin(), it->second.end());
    p.state = PersonState::ToStop;
    p.station = to;
    if (const auto* route = w.routing.find(to, p.finalStation)) p.targetStation = route->nextHop;
    walking::begin(w, pid, std::move(path), w.clock.tick);
    people::schedule(w, pid, p.walkEnd);
    return true;
}

void waitAt(World& w, PersonId pid, StationId at, StationId nextHop) {
    Person& p = w.people[pid];
    if (const auto* route = w.routing.find(at, p.finalStation);
        route && route->walk && walkTransfer(w, pid, at, route->nextHop))
        return;
    Station& station = w.stations[at];
    if (station.waitingCount() >= station.waitingCapacity()) {
        station.rating = std::max(0, station.rating - 4);
        feedback::stationFull(w, pid, at);
        people::travelAbstract(w, pid, {station.sign.x * 2 + 1, station.sign.y * 2 + 1}, p.tripTo2, false);
        return;
    }
    p.state = PersonState::Waiting;
    p.station = at;
    p.targetStation = nextHop;
    p.boardTick = w.clock.tick; // queue arrival until boarding records the ride start
    station.waiting[nextHop].push_back(pid);
    people::schedule(w, pid, w.clock.tick + kWaitReviewTicks);
    positionWaiting(w, at);
}

void removeFromWaiting(World& w, PersonId pid) {
    const Person& p = w.people[pid];
    if (Station* s = w.stations.tryGet(p.station)) {
        auto it = s->waiting.find(p.targetStation);
        if (it != s->waiting.end()) {
            std::erase(it->second, pid);
            if (it->second.empty()) s->waiting.erase(it);
        }
    }
}

TilePos stationPos2(const World& w, StationId id, TilePos fallback2) {
    if (const Station* s = w.stations.tryGet(id)) return {s->sign.x * 2 + 1, s->sign.y * 2 + 1};
    return fallback2;
}

// Two precise doors per passenger carriage, located along the train's actual track history.
struct Door {
    TPoint centre;
    TDir direction;
    i32 height = -1;
};
Door doorForSeat(const World& w, const Vehicle& v, usize seat) {
    i32 back = 0;
    const auto addCar = [&](const VehicleModel& car) {
        if (seat < car.capacity) {
            back += car.length * (seat < (car.capacity + 1u) / 2 ? 4 : 12);
            return true;
        }
        seat -= car.capacity;
        back += car.length * 16 + 4;
        return false;
    };
    if (v.cars.empty())
        addCar(v.info());
    else
        for (const auto model : v.cars)
            if (addCar(vehicleModel(model))) break;
    if (v.freeTrack && !v.runs.empty()) {
        i32 distance = v.progress - back;
        for (usize i = 0; i < v.runs.size(); ++i) {
            const auto run = v.runs[i];
            const auto* edge = w.trackEdges.tryGet(run.edge);
            if (!edge) break;
            if (i > 0) distance += edge->length;
            if (distance < 0) continue;
            const auto sample = track::pointOn(*edge, run.forward, distance);
            const auto& a = w.trackNodes[edge->a];
            const auto& b = w.trackNodes[edge->b];
            auto direction = normalised(i64(b.pos.x) - a.pos.x, i64(b.pos.z) - a.pos.z);
            if (!run.forward) direction = -direction;
            return {sample.p, direction, sample.h};
        }
    }
    TilePos tile = v.tile;
    if (v.info().type == VehicleType::Rail && !v.trail.empty()) {
        i32 remaining = back - v.progress;
        for (usize i = 0; i < v.trail.size(); ++i) {
            tile = v.trail[i].tile;
            if (i == 0 && remaining <= 0) break;
            if (i == 0) continue;
            remaining -= trackLength(trackBetween(v.trail[i].entry, v.trail[i].exit));
            if (remaining <= 0) break;
        }
    }
    const TDir direction = v.exit == Dir::E   ? TDir{kDirOne, 0}
                           : v.exit == Dir::W ? TDir{-kDirOne, 0}
                           : v.exit == Dir::S ? TDir{0, kDirOne}
                                              : TDir{0, -kDirOne};
    return {tileCentre(tile), direction, -1};
}
TPoint doorSide(const Door& door, i32 side, i32 distance) {
    return offset(door.centre, {-door.direction.z, door.direction.x}, side * distance);
}
TilePos halfTile(TPoint p) { return {floorDiv(p.x * 2, kTrackUnits), floorDiv(p.z * 2, kTrackUnits)}; }

void chooseLocalContinuation(World& w, Person& p, StationId at) {
    if (at != p.finalStation || !(w.stations[at].facilities & kFacilityRail)) return;
    const auto endpoint = walking::endpoint(w, p.tripTo2);
    const auto direct = walking::fromStation(w, at, endpoint);
    const i64 directLength = direct ? walking::length(*direct) : INT64_MAX / 4;
    i64 best = direct ? 3 * directLength * kWalkTicksPerTile / kTrackUnits : INT64_MAX;
    std::vector<StationId> destinations;
    accessStations(w, p.tripTo2, destinations);
    for (const auto destination : destinations) {
        if (destination == at || !(w.stations[destination].facilities & (kFacilityBus | kFacilityTram)))
            continue;
        const auto* route = w.routing.find(at, destination);
        if (!route || !route->hasRide) continue;
        const auto finalWalk = walking::fromStation(w, destination, endpoint);
        if (!finalWalk || walking::length(*finalWalk) + 2 * kTrackUnits >= directLength) continue;
        const i64 cost = route->cost + 3 * walking::length(*finalWalk) * kWalkTicksPerTile / kTrackUnits;
        if (cost < best) {
            best = cost;
            p.finalStation = destination;
        }
    }
}

void alight(World& w, PersonId pid, StationId at, VehicleId vid, Vehicle& v, usize seat) {
    Person& p = w.people[pid];
    const Station* from = w.stations.tryGet(p.station);
    const Station* to = w.stations.tryGet(at);
    const i64 tiles = (from && to) ? manhattan(from->sign.x - to->sign.x, from->sign.y - to->sign.y) : 0;
    const i64 ride = w.clock.tick - p.boardTick;
    const Money pay = subsidies::passengerPay(w, v.owner, p.station, at, w.fareIncome(fare(tiles, ride)));
    feedback::alighted(w, pid, vid, p.station, at, tiles, ride);
    if (Company* c = w.companies.tryGet(v.owner)) {
        const Expense cat = v.info().type == VehicleType::Rail   ? Expense::TrainIncome
                            : v.info().type == VehicleType::Ship ? Expense::ShipIncome
                                                                 : Expense::RoadVehicleIncome;
        c->earn(cat, pay);
        ++c->passengersThisYear;
    }
    v.profitThisYear += pay;
    ++v.passengersThisYear;
    p.vehicle = VehicleId{};
    p.walkPath.clear(); p.walkEnd = 0;
    p.station = at;
    p.transitMotion = 2;
    const Door door = doorForSeat(w, v, seat);
    const i32 side = (mix32(p.serial ^ at.value) & 1) ? 1 : -1;
    const TPoint start = doorSide(door, side, 30);
    const TPoint platform =
        v.info().type == VehicleType::Ship && to ? tileCentre(to->sign) : doorSide(door, side, 115);
    p.transitFrom2 = halfTile(start);
    p.transitTo2 = halfTile(platform);
    p.transitBegin = w.clock.tick;
    p.transitEnd =
        p.transitBegin +
        std::max<i64>(8, (distance(start, platform) * kWalkTicksPerTile + kTrackUnits - 1) / kTrackUnits);
    v.loadingUntil = std::max(v.loadingUntil, p.transitEnd);

    chooseLocalContinuation(w, p, at);
    if (at == p.finalStation || !w.stations.contains(p.finalStation)) {
        p.state = PersonState::FromStop;
        auto walk = walking::fromStation(w,at,walking::endpoint(w,p.tripTo2));
        if (walk) {
            auto internal = walking::withinStation(w, at, platform, walk->front().p);
            internal.insert(internal.begin(), {{start, door.height}, {platform, door.height}});
            internal.insert(internal.end(),walk->begin(),walk->end());
            walking::begin(w, pid, std::move(internal), p.transitBegin);
            people::schedule(w,pid,p.walkEnd);
        } else people::travelAbstract(w,pid,p.transitTo2,p.tripTo2,false);
        return;
    }
    const RouteEntry* r = w.routing.find(at, p.finalStation);
    if (!r) {
        people::travelAbstract(w, pid, stationPos2(w, at, p.tripTo2), p.tripTo2, false);
        return;
    }
    walking::begin(w, pid, {{start, door.height}, {platform, door.height}}, p.transitBegin);
    waitAt(w, pid, at, r->nextHop);
}

} // namespace

void markDirty(World& w) { w.routing.dirty = true; }

void updateRouting(World& w) {
    RoutingTable& rt = w.routing;
    if (!rt.dirty) return;
    const bool preserveSchedules = rt.preserveSchedulesOnBuild;
    rt.preserveSchedulesOnBuild = false;
    rt.routes.clear();
    rt.transfers.clear();

    struct Agg {
        i64 travel = 0;
        i64 cycle = 0;
        i32 count = 0;
    };
    std::map<std::pair<u32, u32>, Agg> links;
    for (const auto [vid, v] : w.vehicles) {
        if ((v.stopped && v.state == VehicleState::InDepot) || v.capacity() == 0) continue;
        const std::vector<StationId> stops = stationOrders(w, v);
        if (stops.size() < 2) continue;
        const i32 speed = v.info().type == VehicleType::Rail ? trains::maxSpeed(v) : std::max(1, speedUnits(v.info().maxSpeedKmh));
        struct Leg {
            u32 a, b;
            i64 travel;
        };
        std::vector<Leg> legs;
        i64 cycle = 0;
        for (usize i = 0; i < stops.size(); ++i) {
            const Station& a = w.stations[stops[i]];
            const Station& b = w.stations[stops[(i + 1) % stops.size()]];
            const i64 dist = manhattan(a.sign.x - b.sign.x, a.sign.y - b.sign.y) * 13 / 10 + 1;
            const i64 travel = dist * kTileUnits / speed + kDwellEstimate;
            legs.push_back({stops[i].value, stops[(i + 1) % stops.size()].value, travel});
            cycle += travel;
        }
        for (const Leg& l : legs) {
            Agg& agg = links[{l.a, l.b}];
            agg.travel += l.travel;
            agg.cycle += cycle;
            ++agg.count;
        }
    }

    struct Edge {
        u32 to;
        i64 cost;
        bool walk = false;
    };
    std::map<u32, std::vector<Edge>> adj;
    for (const auto& [key, agg] : links) {
        const i64 travel = agg.travel / agg.count;
        const i64 headway = (agg.cycle / agg.count) / agg.count;
        adj[key.first].push_back({key.second, travel + headway / 2});
    }

    // A train entrance and a nearby street stop form an interchange only when a real
    // pedestrian route joins them. Track distance alone must never create a transfer.
    for (const auto [railId, rail] : w.stations) {
        if (!(rail.facilities & kFacilityRail) || rail.entrances.empty() || !adj.contains(railId.value))
            continue;
        for (const auto [localId, local] : w.stations) {
            if (localId == railId || !(local.facilities & (kFacilityBus | kFacilityTram)) ||
                !adj.contains(localId.value))
                continue;
            bool near = false;
            for (const auto& entrance : rail.entrances)
                for (const auto tile : local.tiles)
                    near = near || chebyshev(entrance.road.x - tile.x, entrance.road.y - tile.y) <= 4;
            if (!near) continue;
            const TPoint meeting{local.sign.x * kTrackUnits + 40, local.sign.y * kTrackUnits + 40};
            auto path = walking::fromStation(w, railId, meeting);
            if (!path) continue;
            const i64 cost = std::max<i64>(1, walking::length(*path) * kWalkTicksPerTile / kTrackUnits);
            rt.transfers[{railId.value, localId.value}] = *path;
            std::reverse(path->begin(), path->end());
            rt.transfers[{localId.value, railId.value}] = std::move(*path);
            adj[railId.value].push_back({localId.value, cost, true});
            adj[localId.value].push_back({railId.value, cost, true});
        }
    }

    for (const auto& [origin, edges] : adj) {
        struct Item {
            i64 cost;
            u32 station;
            u32 firstHop;
            bool walk;
            bool hasRide;
            bool operator>(const Item& o) const {
                return std::tie(cost, station, firstHop, walk, hasRide) >
                       std::tie(o.cost, o.station, o.firstHop, o.walk, o.hasRide);
            }
        };
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        std::unordered_map<u32, RouteEntry> best;
        for (const Edge& e : edges) pq.push({e.cost, e.to, e.to, e.walk, !e.walk});
        while (!pq.empty()) {
            const Item it = pq.top();
            pq.pop();
            if (it.station == origin || best.contains(it.station)) continue;
            best[it.station] =
                RouteEntry{StationId(it.firstHop), static_cast<i32>(std::min<i64>(it.cost, 1 << 30)), it.walk,
                           it.hasRide};
            const auto a = adj.find(it.station);
            if (a == adj.end()) continue;
            for (const Edge& e : a->second)
                if (!best.contains(e.to))
                    pq.push({it.cost + e.cost, e.to, it.firstHop, it.walk, it.hasRide || !e.walk});
        }
        rt.routes[origin] = std::move(best);
    }
    rt.dirty = false;
    rt.builtTick = w.clock.tick;
    w.townLinks.routing = -2;
    // Entrances can change access without changing the station graph. Refresh after every
    // network edit; the first rebuild after loading a current save keeps saved departures.
    if (!preserveSchedules) {
        for (const auto [id, person] : w.people) {
            if (person.state == PersonState::AtHome)
                people::reschedule(w, id);
            else if (person.state == PersonState::Waiting)
                reviewWaiting(w, id);
        }
    }
}

void accessStations(const World& w, TilePos centre2, std::vector<StationId>& out) {
    const TilePos tile{floorDiv(centre2.x, 2), floorDiv(centre2.y, 2)};
    stations::stationsCovering(w, tile, out);
    std::erase_if(out, [&](StationId s) { return !w.stations[s].takesPassengers(); });
    const auto* surface = w.map.tile(tile);
    const auto* building = surface && surface->type == TileType::Building
                               ? w.buildings.tryGet(BuildingId(surface->index))
                               : nullptr;
    if (!building || !w.towns.contains(building->town)) return;
    for (const auto [sid, station] : w.stations)
        if (station.town == building->town && (station.facilities & kFacilityRail) && station.takesPassengers() &&
            std::ranges::find(out, sid) == out.end())
            out.push_back(sid);
}

bool planTrip(const World& w, TilePos from2, TilePos to2, TransitPlan& out) {
    if (w.routing.routes.empty()) return false;
    std::vector<StationId> origins, dests;
    accessStations(w, from2, origins);
    if (origins.empty()) return false;
    accessStations(w, to2, dests);
    if (dests.empty()) return false;
    bool found = false;
    for (const StationId o : origins) {
        const auto originWalk = walking::toStation(w,walking::endpoint(w,from2),o);
        if (!originWalk) continue;
        const i64 walkO = walking::length(*originWalk)*kWalkTicksPerTile/kTrackUnits;
        for (const StationId d : dests) {
            if (o == d) continue;
            const RouteEntry* r = w.routing.find(o, d);
            if (!r || !r->hasRide) continue;
            const auto destinationWalk = walking::fromStation(w,d,walking::endpoint(w,to2));
            if (!destinationWalk) continue;
            const i64 walkD = walking::length(*destinationWalk)*kWalkTicksPerTile/kTrackUnits;
            // Prefer useful feeder services over long walks across the town.
            const i64 total = 3 * (walkO + walkD) + r->cost;
            if (!found || total < out.total) {
                found = true;
                out = TransitPlan{o, r->nextHop, d, walkO, total};
            }
        }
    }
    return found;
}

void arriveAtStation(World& w, PersonId pid) {
    Person& p = w.people[pid];
    if (!w.stations.contains(p.station) || !w.stations.contains(p.targetStation)) {
        people::travelAbstract(w, pid, p.tripFrom2, p.tripTo2, false);
        return;
    }
    if (p.station == p.finalStation) {
        const auto path = walking::fromStation(w, p.station, walking::endpoint(w, p.tripTo2));
        if (path) {
            p.state = PersonState::FromStop;
            walking::begin(w, pid, *path, w.clock.tick);
            people::schedule(w, pid, p.walkEnd);
        } else
            people::travelAbstract(w, pid, stationPos2(w, p.station, p.tripTo2), p.tripTo2, false);
        return;
    }
    const auto* route = w.routing.find(p.station, p.finalStation);
    if (!route && !p.finalStation.valid()) {
        waitAt(w, pid, p.station, p.targetStation);
        return;
    }
    if (!route) {
        people::travelAbstract(w, pid, stationPos2(w, p.station, p.tripTo2), p.tripTo2, false);
        return;
    }
    waitAt(w, pid, p.station, route->nextHop);
}

void reviewWaiting(World& w, PersonId pid) {
    Person& p = w.people[pid];
    if (w.stations.contains(p.station) && w.stations.contains(p.finalStation)) {
        if (const auto* route = w.routing.find(p.station, p.finalStation)) {
            if (route->walk) {
                removeFromWaiting(w, pid);
                if (walkTransfer(w, pid, p.station, route->nextHop)) return;
            }
            if (route->nextHop != p.targetStation) {
                removeFromWaiting(w, pid);
                p.targetStation = route->nextHop;
                w.stations[p.station].waiting[p.targetStation].push_back(pid);
                positionWaiting(w, p.station);
            }
            people::schedule(w, pid, std::max(w.clock.tick, p.walkEnd) + kWaitReviewTicks);
            return;
        }
    }
    giveUpWaiting(w, pid);
}

void giveUpWaiting(World& w, PersonId pid) {
    removeFromWaiting(w, pid);
    Person& p = w.people[pid];
    feedback::gaveUp(w, pid, p.station, w.clock.tick - p.boardTick);
    if (Station* s = w.stations.tryGet(p.station)) s->rating = std::max(0, s->rating - 2);
    people::travelAbstract(w, pid, stationPos2(w, p.station, p.tripFrom2), p.tripTo2, false);
}

i32 exchangeAtStop(World& w, VehicleId vid, StationId sid) {
    Vehicle& v = w.vehicles[vid];
    Station* station = w.stations.tryGet(sid);
    if (!station) return 0;
    i32 moved = cargo::exchangeAtStop(w, vid, sid);
    if (moved) v.loadingUntil = std::max(v.loadingUntil, w.clock.tick + moved * 2 + 8);
    const auto order = v.currentOrder();
    if (order && order->passThrough()) return 0;
    if (v.passengerSeats.size() != v.passengers.size()) {
        v.passengerSeats.clear();
        for (usize i = 0; i < v.passengers.size(); ++i) v.passengerSeats.push_back(static_cast<u16>(i));
    }
    // Every carriage unloads concurrently. Through passengers retain their actual seats.
    std::vector<PersonId> staying;
    std::vector<u16> stayingSeats;
    for (usize i = 0; i < v.passengers.size(); ++i) {
        const PersonId pid = v.passengers[i];
        Person* p = w.people.tryGet(pid);
        if (!p) continue;
        if ((order && (order->flags & kOrderUnloadOnly)) || p->targetStation == sid ||
            p->finalStation == sid || !vehicleVisits(v, p->targetStation)) {
            alight(w, pid, sid, vid, v, v.passengerSeats[i]);
            ++moved;
        } else {
            staying.push_back(pid);
            stayingSeats.push_back(v.passengerSeats[i]);
        }
    }
    v.passengers = std::move(staying);
    v.passengerSeats = std::move(stayingSeats);
    if (order && (order->flags & (kOrderNoLoading | kOrderUnloadOnly))) return moved;
    const StationId next = nextStopAfterCurrent(w, v);
    if (!next.valid() || next == sid) return moved;
    if (v.freeTrack) positionWaiting(w, sid);
    station = w.stations.tryGet(sid);
    auto queue = station->waiting.find(next);
    if (queue == station->waiting.end()) return moved;
    const usize capacity = v.capacity();
    if (v.passengers.size() >= capacity) return moved;
    std::vector<Door> doors;
    std::vector<bool> occupied(capacity, false);
    std::map<std::pair<i32, i32>, i32> doorLoads;
    for (usize seat = 0; seat < capacity; ++seat) doors.push_back(doorForSeat(w, v, seat));
    for (const auto seat : v.passengerSeats)
        if (seat < capacity) {
            occupied[seat] = true;
            ++doorLoads[{doors[seat].centre.x, doors[seat].centre.z}];
        }
    int actualPlatform = 0;
    if (v.freeTrack && !v.runs.empty()) {
        const auto& edge = w.trackEdges[v.runs.front().edge];
        actualPlatform = track::platformNumber(w, sid, track::pointOn(edge, true, edge.length / 2).p);
    }
    std::vector<PersonId> remaining;
    for (const PersonId pid : queue->second) {
        Person* p = w.people.tryGet(pid);
        if (!p || p->state != PersonState::Waiting || p->station != sid) continue;
        if (v.passengers.size() >= capacity || (p->transitMotion == 2 && p->transitEnd > w.clock.tick)) {
            remaining.push_back(pid);
            continue;
        }
        const WalkPoint current =
            p->walkPath.empty()
                ? WalkPoint{station->entrances.empty() ? tileCentre(station->sign)
                                                       : station->entrances.front().path.back()}
                : walking::position(*p, w.clock.tick);
        if (v.freeTrack) {
            // A walk to a waiting spot need not finish: anyone already on the correct platform
            // can head directly for a door. Concourse / street walkers never hold up the train.
            const auto platform = track::platformNear(w, sid, current.p, kPlatformHalfWidth);
            if (!actualPlatform || !platform.valid() ||
                track::platformNumber(w, sid, current.p) != actualPlatform ||
                (current.height >= 0 &&
                 std::abs(current.height - w.trackNodes[w.trackEdges[platform].a].h) > 8)) {
                remaining.push_back(pid);
                continue;
            }
        }
        usize best = capacity;
        i64 bestCost = std::numeric_limits<i64>::max();
        for (usize seat = 0; seat < capacity; ++seat) {
            if (occupied[seat]) continue;
            const auto& door = doors[seat];
            const i64 cost =
                distance(current.p, door.centre) + doorLoads[{door.centre.x, door.centre.z}] * 12;
            if (cost < bestCost) {
                best = seat;
                bestCost = cost;
            }
        }
        if (best == capacity) {
            remaining.push_back(pid);
            continue;
        }
        const Door& door = doors[best];
        const TDir normal{-door.direction.z, door.direction.x};
        const i64 lateral =
            (i64(current.p.x) - door.centre.x) * normal.x + (i64(current.p.z) - door.centre.z) * normal.z;
        const i32 side = lateral < 0 ? -1 : 1;
        const TPoint approach = doorSide(door, side, 115), entrance = doorSide(door, side, 30);
        std::vector<WalkPoint> path;
        if (v.freeTrack)
            path = {{current.p, door.height}, {approach, door.height}, {entrance, door.height}};
        else
            path = walking::withinStation(w, sid, current.p, entrance);
        const i64 waited = w.clock.tick - p->boardTick; // boardTick held the queue arrival
        p->state = PersonState::Riding;
        p->vehicle = vid;
        p->boardTick = w.clock.tick;
        p->nextEvent = -1;
        p->transitMotion = 1;
        p->transitFrom2 = halfTile(current.p);
        p->transitTo2 = halfTile(entrance);
        p->transitBegin = w.clock.tick;
        walking::begin(w, pid, std::move(path), p->transitBegin);
        p->transitEnd = std::max(p->transitBegin + 8, p->walkEnd);
        p->walkEnd = p->transitEnd;
        v.loadingUntil = std::max(v.loadingUntil, p->transitEnd);
        v.passengers.push_back(pid);
        v.passengerSeats.push_back(static_cast<u16>(best));
        feedback::boarded(w, pid, vid, sid, waited);
        occupied[best] = true;
        ++doorLoads[{door.centre.x, door.centre.z}];
        ++station->boardedThisMonth;
        ++moved;
    }
    queue->second = std::move(remaining);
    if (queue->second.empty()) station->waiting.erase(queue);
    if (moved > 0) station->rating = std::min(255, station->rating + 1);
    return moved;
}

void abandonRide(World& w, PersonId pid) {
    Person* p = w.people.tryGet(pid);
    if (!p || p->state != PersonState::Riding) return;
    TilePos here = p->tripFrom2;
    if (const Vehicle* v = w.vehicles.tryGet(p->vehicle)) here = {v->tile.x * 2 + 1, v->tile.y * 2 + 1};
    p->vehicle = VehicleId{};
    people::travelAbstract(w, pid, here, p->tripTo2, false);
}

Money fare(i64 tiles, i64 ticks) {
    i64 cents = 100 + tiles * 15;
    const i64 expected = std::max<i64>(tiles, 1) * 14;
    if (ticks > expected * 3) cents /= 2;
    return (cents + 50) / 100;
}

} // namespace passengers

} // namespace tp
