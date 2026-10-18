#include "sim/passengers.hpp"

#include "core/math.hpp"
#include "sim/person.hpp"
#include "sim/rail.hpp"
#include "sim/vehicle.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <queue>

namespace tp {

const RouteEntry* RoutingTable::find(StationId from, StationId to) const {
    const auto it = routes.find(from.value);
    if (it == routes.end()) return nullptr;
    const auto jt = it->second.find(to.value);
    return jt == it->second.end() ? nullptr : &jt->second;
}

namespace passengers {

namespace {

constexpr i64 kMaxWaitTicks = 3 * kTicksPerHour;
constexpr i64 kDwellEstimate = 4 * kTicksPerMinute;

std::vector<StationId> stationOrders(const World& w, const Vehicle& v) {
    std::vector<StationId> list;
    for (const Order& o : v.orders) {
        if (o.kind != OrderKind::Station || o.passThrough() || !w.stations.contains(o.station)) continue;
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

void waitAt(World& w, PersonId pid, StationId at, StationId nextHop) {
    Person& p = w.people[pid];
    Station& station = w.stations[at];
    if (station.waitingCount() >= station.waitingCapacity()) {
        station.rating = std::max(0, station.rating - 4);
        people::travelAbstract(w, pid, {station.sign.x * 2 + 1, station.sign.y * 2 + 1}, p.tripTo2, false);
        return;
    }
    p.state = PersonState::Waiting;
    p.station = at;
    p.targetStation = nextHop;
    station.waiting[nextHop].push_back(pid);
    people::schedule(w, pid, w.clock.tick + kMaxWaitTicks);
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

// The person reached a station they get off at: pay, then transfer or walk on.
// Door position of the carriage containing this seat, in half-tile coordinates.
TilePos doorPosition2(const World& w, const Vehicle& v, usize seat) {
    TilePos tile = v.tile;
    if (v.freeTrack && !v.runs.empty()) {
        i32 back = 0;
        for (const u16 model : v.cars) {
            const auto& car = vehicleModel(model);
            if (seat < car.capacity) { back += car.length * 8; break; }
            seat -= std::min<usize>(seat, car.capacity);
            back += car.length * 16 + 4;
        }
        i32 distance = v.progress - back;
        for (usize i = 0; i < v.runs.size(); ++i) {
            const auto run = v.runs[i];
            const auto* edge = w.trackEdges.tryGet(run.edge);
            if (!edge) break;
            if (i > 0) distance += edge->length;
            if (distance >= 0) {
                const auto sample = track::pointOn(*edge, run.forward, distance);
                return {floorDiv(sample.p.x * 2, kTrackUnits), floorDiv(sample.p.z * 2, kTrackUnits)};
            }
        }
    }
    if (v.info().type == VehicleType::Rail && !v.trail.empty()) {
        i32 offset = 0;
        for (const u16 model : v.cars) {
            const auto& car = vehicleModel(model);
            if (seat < car.capacity) { offset += car.length * 8; break; }
            seat -= std::min<usize>(seat, car.capacity);
            offset += car.length * 16 + 4;
        }
        i32 remaining = offset - v.progress;
        for (usize i = 0; i < v.trail.size(); ++i) {
            tile = v.trail[i].tile;
            if (i == 0 && remaining <= 0) break;
            if (i == 0) continue;
            remaining -= trackLength(trackBetween(v.trail[i].entry, v.trail[i].exit));
            if (remaining <= 0) break;
        }
    }
    return {tile.x * 2 + 1, tile.y * 2 + 1};
}

void alight(World& w, PersonId pid, StationId at, Vehicle& v, usize seat, i32 sequence) {
    Person& p = w.people[pid];
    const Station* from = w.stations.tryGet(p.station);
    const Station* to = w.stations.tryGet(at);
    const i64 tiles = (from && to) ? manhattan(from->sign.x - to->sign.x, from->sign.y - to->sign.y) : 0;
    const Money pay = w.fareIncome(fare(tiles, w.clock.tick - p.boardTick));
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
    p.transitFrom2 = doorPosition2(w, v, seat);
    p.transitTo2 = p.transitFrom2;
    // Walk sideways onto the platform / kerb (or up the dock gangway).
    if (v.info().type == VehicleType::Ship && to) p.transitTo2 = {to->sign.x * 2 + 1, to->sign.y * 2 + 1};
    else if (v.exit == Dir::E || v.exit == Dir::W) ++p.transitTo2.y;
    else ++p.transitTo2.x;
    p.transitBegin = w.clock.tick + sequence * 2;
    p.transitEnd = p.transitBegin + 8;

    if (at == p.finalStation || !w.stations.contains(p.finalStation)) {
        p.state = PersonState::FromStop;
        auto walk = walking::fromStation(w,at,walking::endpoint(w,p.tripTo2));
        if (walk) {
            auto internal = walking::withinStation(w,at,{p.transitTo2.x*kTrackUnits/2,p.transitTo2.y*kTrackUnits/2},walk->front().p);
            internal.insert(internal.end(),walk->begin(),walk->end());
            walking::begin(w,pid,std::move(internal),p.transitEnd);
            people::schedule(w,pid,p.walkEnd);
        } else people::travelAbstract(w,pid,p.transitTo2,p.tripTo2,false);
        return;
    }
    const RouteEntry* r = w.routing.find(at, p.finalStation);
    if (!r) {
        people::travelAbstract(w, pid, stationPos2(w, at, p.tripTo2), p.tripTo2, false);
        return;
    }
    p.walkPath = {{{p.transitTo2.x*kTrackUnits/2,p.transitTo2.y*kTrackUnits/2}}};
    p.walkBegin = p.walkEnd = p.transitEnd;
    waitAt(w, pid, at, r->nextHop);
}

} // namespace

void markDirty(World& w) { w.routing.dirty = true; }

void updateRouting(World& w) {
    RoutingTable& rt = w.routing;
    if (!rt.dirty) return;
    rt.routes.clear();

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
        const i32 speed = std::max(1, speedUnits(v.info().maxSpeedKmh));
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
    };
    std::map<u32, std::vector<Edge>> adj;
    for (const auto& [key, agg] : links) {
        const i64 travel = agg.travel / agg.count;
        const i64 headway = (agg.cycle / agg.count) / agg.count;
        adj[key.first].push_back({key.second, travel + headway / 2});
    }

    for (const auto& [origin, edges] : adj) {
        struct Item {
            i64 cost;
            u32 station;
            u32 firstHop;
            bool operator>(const Item& o) const {
                return cost != o.cost ? cost > o.cost : (station != o.station ? station > o.station : firstHop > o.firstHop);
            }
        };
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        std::unordered_map<u32, RouteEntry> best;
        for (const Edge& e : edges) pq.push({e.cost, e.to, e.to});
        while (!pq.empty()) {
            const Item it = pq.top();
            pq.pop();
            if (it.station == origin || best.contains(it.station)) continue;
            best[it.station] = RouteEntry{StationId(it.firstHop), static_cast<i32>(std::min<i64>(it.cost, 1 << 30))};
            const auto a = adj.find(it.station);
            if (a == adj.end()) continue;
            for (const Edge& e : a->second)
                if (!best.contains(e.to)) pq.push({it.cost + e.cost, e.to, it.firstHop});
        }
        rt.routes[origin] = std::move(best);
    }
    rt.dirty = false;
    rt.builtTick = w.clock.tick;
}

bool planTrip(const World& w, TilePos from2, TilePos to2, TransitPlan& out) {
    if (w.routing.routes.empty()) return false;
    std::vector<StationId> origins, dests;
    stations::stationsCovering(w, {from2.x / 2, from2.y / 2}, origins);
    if (origins.empty()) return false;
    stations::stationsCovering(w, {to2.x / 2, to2.y / 2}, dests);
    if (dests.empty()) return false;
    bool found = false;
    for (const StationId o : origins) {
        const auto originWalk = walking::toStation(w,walking::endpoint(w,from2),o);
        if (!originWalk) continue;
        const i64 walkO = walking::length(*originWalk)*kWalkTicksPerTile/kTrackUnits;
        for (const StationId d : dests) {
            if (o == d) continue;
            const RouteEntry* r = w.routing.find(o, d);
            if (!r) continue;
            const auto destinationWalk = walking::fromStation(w,d,walking::endpoint(w,to2));
            if (!destinationWalk) continue;
            const i64 walkD = walking::length(*destinationWalk)*kWalkTicksPerTile/kTrackUnits;
            const i64 total = walkO + r->cost + walkD;
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
    waitAt(w, pid, p.station, p.targetStation);
}

void giveUpWaiting(World& w, PersonId pid) {
    removeFromWaiting(w, pid);
    Person& p = w.people[pid];
    if (Station* s = w.stations.tryGet(p.station)) s->rating = std::max(0, s->rating - 2);
    people::travelAbstract(w, pid, stationPos2(w, p.station, p.tripFrom2), p.tripTo2, false);
}

i32 exchangeAtStop(World& w, VehicleId vid, StationId sid) {
    Vehicle& v = w.vehicles[vid];
    Station* station = w.stations.tryGet(sid);
    if (!station) return 0;
    i32 moved = cargo::exchangeAtStop(w, vid, sid);
    const auto o = v.currentOrder();
    if (o && o->passThrough()) return 0;
    // Unload.
    std::vector<PersonId> staying;
    staying.reserve(v.passengers.size());
    usize seat = 0;
    for (const PersonId pid : v.passengers) {
        const usize seatNumber = seat++;
        Person* p = w.people.tryGet(pid);
        if (!p) continue;
        if ((o && (o->flags & kOrderUnloadOnly)) || p->targetStation == sid || p->finalStation == sid || !vehicleVisits(v, p->targetStation)) {
            alight(w, pid, sid, v, seatNumber, moved);
            ++moved;
        } else {
            staying.push_back(pid);
        }
    }
    v.passengers = std::move(staying);
    // Load passengers heading for our next stop.
    if (o && (o->flags & (kOrderNoLoading | kOrderUnloadOnly))) return moved;
    const StationId next = nextStopAfterCurrent(w, v);
    if (!next.valid() || next == sid) return moved;
    station = w.stations.tryGet(sid);
    auto it = station->waiting.find(next);
    if (it == station->waiting.end()) return moved;
    std::vector<PersonId>& queue = it->second;
    const usize capacity = v.capacity();
    usize taken = 0;
    while (taken < queue.size() && v.passengers.size() < capacity) {
        if (const Person* nextPerson = w.people.tryGet(queue[taken]); nextPerson && nextPerson->transitMotion == 2 &&
            std::max(nextPerson->transitEnd,nextPerson->walkEnd) > w.clock.tick) break; // finish walking off the previous vehicle first
        const PersonId pid = queue[taken++];
        Person* p = w.people.tryGet(pid);
        if (!p || p->state != PersonState::Waiting || p->station != sid) continue;
        p->state = PersonState::Riding;
        p->vehicle = vid;
        p->boardTick = w.clock.tick;
        p->nextEvent = -1;
        p->transitMotion = 1;
        const TPoint waiting = p->walkPath.empty() ? TPoint{station->sign.x*kTrackUnits+40,station->sign.y*kTrackUnits+40} : p->walkPath.back().p;
        p->transitFrom2 = {waiting.x*2/kTrackUnits,waiting.z*2/kTrackUnits};
        p->transitTo2 = doorPosition2(w, v, v.passengers.size());
        p->transitBegin = w.clock.tick + moved * 2;
        auto boarding = walking::withinStation(w,sid,waiting,{p->transitTo2.x*kTrackUnits/2,p->transitTo2.y*kTrackUnits/2});
        walking::begin(w,pid,std::move(boarding),p->transitBegin);
        p->transitEnd = std::max(p->transitBegin+8,p->walkEnd);
        p->walkEnd = p->transitEnd;
        v.loadingUntil = std::max(v.loadingUntil,p->transitEnd);
        v.passengers.push_back(pid);
        ++station->boardedThisMonth;
        ++moved;
    }
    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(taken));
    if (queue.empty()) station->waiting.erase(it);
    station->rating = std::min(255, station->rating + 1);
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
