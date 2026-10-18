#include "sim/passengers.hpp"

#include "core/math.hpp"
#include "sim/person.hpp"
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

i64 tilesBetween2(TilePos a2, TilePos b2) {
    const i64 dx = a2.x - b2.x, dy = a2.y - b2.y;
    return (static_cast<i64>(isqrt(static_cast<u64>(dx * dx + dy * dy))) + 1) / 2;
}

// Walking distance (tiles) from a half-tile position to the nearest tile of a station.
i64 walkTiles(const Station& s, TilePos p2) {
    i64 best = std::numeric_limits<i64>::max();
    for (const TilePos t : s.tiles) best = std::min(best, tilesBetween2(p2, {t.x * 2 + 1, t.y * 2 + 1}));
    return best;
}

std::vector<StationId> stationOrders(const World& w, const Vehicle& v) {
    std::vector<StationId> list;
    for (const Order& o : v.orders) {
        if (o.kind != OrderKind::Station || !w.stations.contains(o.station)) continue;
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
        if (o.kind == OrderKind::Station && w.stations.contains(o.station)) return o.station;
    }
    return StationId{};
}

bool vehicleVisits(const Vehicle& v, StationId s) {
    for (const Order& o : v.orders)
        if (o.kind == OrderKind::Station && o.station == s) return true;
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
void alight(World& w, PersonId pid, StationId at, Vehicle& v) {
    Person& p = w.people[pid];
    const Station* from = w.stations.tryGet(p.station);
    const Station* to = w.stations.tryGet(at);
    const i64 tiles = (from && to) ? manhattan(from->sign.x - to->sign.x, from->sign.y - to->sign.y) : 0;
    const Money pay = fare(tiles, w.clock.tick - p.boardTick);
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
    p.station = at;

    if (at == p.finalStation || !w.stations.contains(p.finalStation)) {
        p.state = PersonState::FromStop;
        const i64 walk = tilesBetween2(stationPos2(w, at, p.tripTo2), p.tripTo2);
        people::schedule(w, pid, w.clock.tick + std::max<i64>(1, walk) * kWalkTicksPerTile);
        return;
    }
    const RouteEntry* r = w.routing.find(at, p.finalStation);
    if (!r) {
        people::travelAbstract(w, pid, stationPos2(w, at, p.tripTo2), p.tripTo2, false);
        return;
    }
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
        if (v.stopped && v.state == VehicleState::InDepot) continue;
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
        const Station& so = w.stations[o];
        const i64 walkO = walkTiles(so, from2) * kWalkTicksPerTile;
        for (const StationId d : dests) {
            if (o == d) continue;
            const RouteEntry* r = w.routing.find(o, d);
            if (!r) continue;
            const i64 walkD = walkTiles(w.stations[d], to2) * kWalkTicksPerTile;
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
    i32 moved = 0;
    // Unload.
    std::vector<PersonId> staying;
    staying.reserve(v.passengers.size());
    for (const PersonId pid : v.passengers) {
        Person* p = w.people.tryGet(pid);
        if (!p) continue;
        if (p->targetStation == sid || p->finalStation == sid || !vehicleVisits(v, p->targetStation)) {
            alight(w, pid, sid, v);
            ++moved;
        } else {
            staying.push_back(pid);
        }
    }
    v.passengers = std::move(staying);
    // Load passengers heading for our next stop.
    const auto o = v.currentOrder();
    if (o && (o->flags & kOrderNoLoading)) return moved;
    const StationId next = nextStopAfterCurrent(w, v);
    if (!next.valid() || next == sid) return moved;
    station = w.stations.tryGet(sid);
    auto it = station->waiting.find(next);
    if (it == station->waiting.end()) return moved;
    std::vector<PersonId>& queue = it->second;
    const usize capacity = v.capacity();
    usize taken = 0;
    while (taken < queue.size() && v.passengers.size() < capacity) {
        const PersonId pid = queue[taken++];
        Person* p = w.people.tryGet(pid);
        if (!p || p->state != PersonState::Waiting || p->station != sid) continue;
        p->state = PersonState::Riding;
        p->vehicle = vid;
        p->boardTick = w.clock.tick;
        p->nextEvent = -1;
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
