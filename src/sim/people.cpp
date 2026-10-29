#include "sim/person.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/passengers.hpp"
#include "sim/world.hpp"
#include "sim/private_cars.hpp"

#include <algorithm>
#include <map>
#include <optional>

namespace tp::people {

namespace {

Building* resolve(World& w, const BuildingRef& r) {
    Building* b = w.buildings.tryGet(r.id);
    return (b && b->serial == r.serial) ? b : nullptr;
}
const Building* resolve(const World& w, const BuildingRef& r) {
    const Building* b = w.buildings.tryGet(r.id);
    return (b && b->serial == r.serial) ? b : nullptr;
}

i64 distanceTiles(TilePos a2, TilePos b2) {
    const i64 dx = a2.x - b2.x;
    const i64 dy = a2.y - b2.y;
    return (static_cast<i64>(isqrt(static_cast<u64>(dx * dx + dy * dy))) + 1) / 2;
}

bool findVacancy(World& w, TownId townId, PersonId pid) {
    Town* t = w.towns.tryGet(townId);
    if (!t || t->jobSites.empty()) return false;
    const usize n = t->jobSites.size();
    for (usize k = 0; k < n; ++k) {
        const usize idx = (t->jobCursor + k) % n;
        const BuildingId bid = t->jobSites[idx];
        Building* b = w.buildings.tryGet(bid);
        if (!b || b->workers >= b->info().jobs) continue;
        ++b->workers;
        ++t->workers;
        w.people[pid].work = b->ref(bid);
        t->jobCursor = static_cast<u32>(idx);
        return true;
    }
    return false;
}

// Picks the destination of a day out from the person's town to a town linked to it by public
// transport. Rail serves the whole town through connected streets. Bigger towns draw more
// visitors. Nothing when there is no walkable round trip.
std::optional<BuildingRef> planVisit(const World& w, const Person& p, i32 day, bool business) {
    const Building* home = resolve(w, p.home);
    if (!home) return std::nullopt;
    const std::vector<TownLink>& links = townLinks(w, p.town);
    if (links.empty()) return std::nullopt;
    // A station with links must be within reach of home.
    std::vector<StationId> near;
    const TilePos centre = home->centre2();
    passengers::accessStations(w, centre, near);
    i64 total = 0;
    std::vector<const TownLink*> usable;
    for (const TownLink& link : links) {
        if (!std::ranges::any_of(link.origins,
                                 [&](StationId s) { return std::ranges::find(near, s) != near.end(); }))
            continue;
        const Town* t = w.towns.tryGet(link.town);
        if (!t) continue;
        usable.push_back(&link);
        total += t->population + 50;
    }
    if (total <= 0) return std::nullopt;
    const u32 h = mix32(p.serial * 0x27D4EB2Du ^ static_cast<u32>(day) * 0x165667B1u);
    i64 pick = static_cast<i64>(h % static_cast<u32>(total));
    const TownLink* link = usable.front();
    for (const TownLink* l : usable) {
        const i64 weight = w.towns[l->town].population + 50;
        if (pick < weight) {
            link = l;
            break;
        }
        pick -= weight;
    }
    // Try every connected town if the preferred one has no usable destination. A city's rail
    // station serves buildings across its connected streets, beyond the local stop catchment.
    const usize start = static_cast<usize>(std::ranges::find(usable, link) - usable.begin());
    for (usize townIndex = 0; townIndex < usable.size(); ++townIndex) {
        const Town& t = w.towns[usable[(start + townIndex) % usable.size()]->town];
        std::vector<BuildingId> choices;
        for (const BuildingId bid : t.buildings) {
            const Building* b = w.buildings.tryGet(bid);
            if (!b) continue;
            const BuildingCategory cat = b->info().category;
            const bool fits = business ? b->info().jobs > 0
                                       : cat == BuildingCategory::Retail || cat == BuildingCategory::Civic;
            if (fits) choices.push_back(bid);
        }
        for (usize i = 0; i < choices.size(); ++i) {
            const BuildingId dest = choices[((h >> 7) + i) % choices.size()];
            const TilePos there = w.buildings[dest].centre2();
            passengers::TransitPlan outbound, inbound;
            if (!passengers::planTrip(w, centre, there, outbound) ||
                !passengers::planTrip(w, there, centre, inbound))
                continue;
            return w.buildings[dest].ref(dest);
        }
    }
    return std::nullopt;
}

void scheduleFromHome(World& w, PersonId id) {
    refreshTownLinks(w);
    applyHomePlan(w, id, planFromHome(w, w.people[id]));
}

void startTrip(World& w, PersonId id, TilePos from2, TilePos to2) {
    Person& p = w.people[id];
    const i64 dist = distanceTiles(from2, to2);
    p.tripStart = w.clock.tick;
    p.tripFrom2 = from2;
    p.tripTo2 = to2;
    Town* town = w.towns.tryGet(p.town);
    if (town) ++town->thisMonth.trips;
    if (dist > kMaxWalkTiles - 2) {
        passengers::TransitPlan plan;
        if (passengers::planTrip(w, from2, to2, plan)) {
            // An available service is the preferred mode, even for car owners and long routes.
            p.state = PersonState::ToStop;
            p.station = plan.origin;
            p.targetStation = plan.nextHop;
            p.finalStation = plan.destination;
            auto walk = walking::toStation(w, walking::endpoint(w, from2), p.station);
            if (walk && walking::begin(w, id, std::move(*walk), w.clock.tick)) {
                if (town) ++town->thisMonth.transit;
                schedule(w, id, p.walkEnd);
                return;
            }
        }
    }
    travelAbstract(w, id, from2, to2, false);
}

void arrive(World& w, PersonId id) {
    Person& p = w.people[id];
    if (p.state == PersonState::FromStop)
        if (Town* town = w.towns.tryGet(towns::townAt(w, {floorDiv(p.tripTo2.x, 2), floorDiv(p.tripTo2.y, 2)}))) ++town->arrivals;
    const i64 now = w.clock.tick;
    switch (p.purpose) {
    case TripPurpose::Work: {
        p.state = PersonState::AtWork;
        // Learn from the commute: leave earlier or later next time to arrive on time.
        const i64 commute = std::clamp<i64>((now - p.tripStart) / kTicksPerMinute, 5, 300);
        p.commuteMinutes = static_cast<u16>((p.commuteMinutes * 3 + commute) / 4);
        p.departMinute = static_cast<u16>(std::max<i64>(4 * 60, p.arriveMinute - p.commuteMinutes - 10));
        const i64 planned =
            w.clock.tickOfDay(w.clock.day()) + (p.departMinute + p.workMinutes + 45) * kTicksPerMinute;
        schedule(w, id, std::max(planned, now + 2 * kTicksPerHour));
        break;
    }
    case TripPurpose::Shop: {
        p.state = PersonState::AtShop;
        const i64 stay = 45 + mix32(p.serial ^ static_cast<u32>(now)) % 90;
        schedule(w, id, now + stay * kTicksPerMinute);
        break;
    }
    case TripPurpose::Visit: {
        // A few hours in the other town (a business day is longer), then home.
        const Building* dest = resolve(w, p.visit);
        const bool business = dest && dest->info().category != BuildingCategory::Retail &&
                              dest->info().category != BuildingCategory::Civic;
        p.state = business ? PersonState::AtWork : PersonState::AtShop;
        const i64 stay = (business ? 180 : 90) + mix32(p.serial ^ static_cast<u32>(now)) % 180;
        schedule(w, id, now + stay * kTicksPerMinute);
        break;
    }
    case TripPurpose::Home:
    case TripPurpose::None:
        p.state = PersonState::AtHome;
        p.visit = {};
        scheduleFromHome(w, id);
        break;
    }
}

void handleEvent(World& w, PersonId id);

} // namespace

void travelAbstract(World& w, PersonId id, TilePos from2, TilePos to2, bool newTrip) {
    Person& p = w.people[id];
    privatecars::release(w, id);
    p.carPath.clear();
    p.carStarted = false;
    p.carOwner = p.carOwner || privatecars::ownsCar(p.serial, w.clock.date().year);
    const i64 dist = distanceTiles(from2, to2);
    Town* town = w.towns.tryGet(p.town);
    if (newTrip) {
        p.tripStart = w.clock.tick;
        if (town) ++town->thisMonth.trips;
    }
    p.tripFrom2 = from2;
    p.tripTo2 = to2;
    p.walkPath.clear();
    auto walk = dist <= kMaxWalkTiles ? walking::route(w,walking::endpoint(w,from2),walking::endpoint(w,to2)) : std::nullopt;
    i64 duration;
    if (walk && walking::length(*walk) <= kMaxWalkTiles*kTrackUnits) {
        p.state = PersonState::Walking;
        walking::begin(w,id,std::move(*walk),w.clock.tick);
        duration = p.walkEnd-w.clock.tick;
        if (town) ++town->thisMonth.walked;
    } else if (privatecars::begin(w, id, from2, to2)) {
        if (town) ++town->thisMonth.drove;
        return;
    } else {
        p.state = PersonState::Lift;
        duration = dist * kLiftTicksPerTile + kLiftOverheadTicks;
        if (town) ++town->thisMonth.lifts;
    }
    schedule(w, id, w.clock.tick + duration);
}

namespace {

void handleEvent(World& w, PersonId id) {
    Person& p = w.people[id];
    p.nextEvent = -1;
    Building* home = resolve(w, p.home);
    if (!home) {
        // The home was demolished: the person moves away.
        remove(w, id);
        return;
    }
    switch (p.state) {
    case PersonState::AtHome: {
        if (p.purpose == TripPurpose::None) {
            scheduleFromHome(w, id);
            return;
        }
        if (p.purpose == TripPurpose::Shop) {
            // Occasionally switch to the town's current shopping destination.
            Building* shop = resolve(w, p.shop);
            const Town* town = w.towns.tryGet(p.town);
            if (!shop || (town && shop->town != town->shopTown && mix32(p.serial ^ static_cast<u32>(w.clock.tick)) % 10 == 0))
                assignShop(w, id);
        }
        Building* dest = resolve(w, p.purpose == TripPurpose::Work    ? p.work
                                    : p.purpose == TripPurpose::Visit ? p.visit
                                                                      : p.shop);
        if (!dest && p.purpose == TripPurpose::Visit) {
            p.visit = {};
            scheduleFromHome(w, id);
            return;
        }
        if (!dest) {
            if (p.purpose == TripPurpose::Work) {
                p.work = {};
                if (Town* t = w.towns.tryGet(p.town)) t->unemployed.push_back(id);
            } else {
                p.shop = {};
            }
            scheduleFromHome(w, id);
            return;
        }
        startTrip(w, id, home->centre2(), dest->centre2());
        return;
    }
    case PersonState::AtWork:
    case PersonState::AtShop:
        p.purpose = TripPurpose::Home;
        startTrip(w, id, p.tripTo2, home->centre2());
        return;
    case PersonState::Walking:
    case PersonState::Lift:
    case PersonState::FromStop:
        arrive(w, id);
        return;
    case PersonState::Driving:
        if (!privatecars::advance(w, id)) arrive(w, id);
        return;
    case PersonState::ToStop:
        passengers::arriveAtStation(w, id);
        return;
    case PersonState::Waiting:
        passengers::reviewWaiting(w, id);
        return;
    case PersonState::Riding:
        return; // the vehicle drives this leg
    }
}

} // namespace

PersonId create(World& w, BuildingId homeId) {
    const PersonId id = w.people.create();
    {
        Building& home = w.buildings[homeId];
        Person& p = w.people[id];
        Town* town = w.towns.tryGet(home.town);
        Rng& rng = town ? town->rng : w.rng;
        p.serial = ++w.nextSerial;
        p.home = home.ref(homeId);
        p.town = home.town;
        p.arriveMinute = static_cast<u16>(7 * 60 + 30 + rng.below(120));
        p.departMinute = static_cast<u16>(p.arriveMinute - p.commuteMinutes);
        p.workMinutes = static_cast<u16>(7 * 60 + rng.below(150));
        // Preserve the old ownership draw so this change does not alter town layouts.
        (void)rng.below(100);
        p.carOwner = privatecars::ownsCar(p.serial, w.clock.date().year);
        p.nameSeed = rng.next();
        p.state = PersonState::AtHome;
        p.tripTo2 = home.centre2();
        ++home.residents;
    }
    assignJob(w, id);
    assignShop(w, id);
    scheduleFromHome(w, id);
    return id;
}

void remove(World& w, PersonId id) {
    Person& p = w.people[id];
    privatecars::release(w, id);
    if (p.state == PersonState::Waiting) {
        if (Station* s = w.stations.tryGet(p.station)) {
            auto it = s->waiting.find(p.targetStation);
            if (it != s->waiting.end()) {
                std::erase(it->second, id);
                if (it->second.empty()) s->waiting.erase(it);
            }
        }
    } else if (p.state == PersonState::Riding) {
        if (Vehicle* v = w.vehicles.tryGet(p.vehicle)) {
            const auto it = std::ranges::find(v->passengers, id);
            if (it != v->passengers.end()) {
                if (v->passengerSeats.size() == v->passengers.size())
                    v->passengerSeats.erase(v->passengerSeats.begin() + (it - v->passengers.begin()));
                v->passengers.erase(it);
            }
        }
    }
    if (Building* b = resolve(w, p.home); b && b->residents > 0) --b->residents;
    if (Building* b = resolve(w, p.work); b && b->workers > 0) {
        --b->workers;
        if (Town* t = w.towns.tryGet(b->town); t && t->workers > 0) --t->workers;
    }
    if (Building* b = resolve(w, p.shop); b && b->customers > 0) --b->customers;
    w.people.destroy(id);
}

HomePlan planFromHome(const World& w, const Person& p) {
    const i64 now = w.clock.tick;
    const i32 today = w.clock.day();
    // One outing per day. Opening a route after the morning departure still attracts people
    // today, but coming home does not immediately send them out on another trip.
    const bool returnedToday = p.purpose == TripPurpose::Home && p.tripStart >= w.clock.tickOfDay(today);
    for (i32 d = today + (returnedToday ? 1 : 0); d < today + 8; ++d) {
        const i64 dayStart = w.clock.tickOfDay(d);
        HomePlan plan;
        const auto visit = [&](i64 minute, bool business) {
            i64 t = dayStart + minute * kTicksPerMinute;
            if (t <= now && d == today && w.clock.minuteOfDay() < 20 * 60)
                t = now + (15 + mix32(p.serial ^ static_cast<u32>(d)) % 105) * kTicksPerMinute;
            if (t <= now) return false;
            const auto destination = planVisit(w, p, d, business);
            if (!destination) return false;
            plan = {TripPurpose::Visit, *destination, t};
            return true;
        };
        if (!isWeekend(weekdayFromDays(d))) {
            if (p.employed() && visit(p.departMinute, true)) return plan;
            if (!p.employed() && visit(10 * 60 + mix32(p.serial ^ static_cast<u32>(d)) % 180, false)) return plan;
            if (p.employed()) {
                const i64 jitter = static_cast<i64>(mix32(p.serial * 31u + static_cast<u32>(d)) % 21) - 10;
                const i64 t = dayStart + (p.departMinute + jitter) * kTicksPerMinute;
                if (t > now) return {TripPurpose::Work, {}, t};
            }
        } else if (visit(9 * 60 + mix32(p.serial ^ (static_cast<u32>(d) * 2654435761u)) % 240, false)) {
            return plan;
        } else if (p.shop.valid()) {
            const i64 minute = 9 * 60 + mix32(p.serial ^ (static_cast<u32>(d) * 2654435761u)) % 300;
            const i64 t = dayStart + minute * kTicksPerMinute;
            if (t > now) return {TripPurpose::Shop, {}, t};
        }
    }
    return {TripPurpose::None, {}, w.clock.tickOfDay(today + 1) + 8 * kTicksPerHour};
}

void applyHomePlan(World& w, PersonId id, const HomePlan& plan) {
    Person& p = w.people[id];
    p.visit = plan.visit;
    p.purpose = plan.purpose;
    schedule(w, id, plan.tick);
}

const std::vector<TownLink>& linkedTowns(World& w, TownId town) {
    refreshTownLinks(w);
    return townLinks(w, town);
}

const std::vector<TownLink>& townLinks(const World& w, TownId town) {
    static const std::vector<TownLink> kNone;
    TP_ASSERT(w.townLinks.routing == w.routing.builtTick);
    const auto it = w.townLinks.byTown.find(town.value);
    return it == w.townLinks.byTown.end() ? kNone : it->second;
}

void refreshTownLinks(World& w) {
    TownLinks& links = w.townLinks;
    if (links.routing != w.routing.builtTick) {
        links.routing = w.routing.builtTick;
        links.byTown.clear();
        std::map<u32, std::vector<StationId>> byTown;
        for (const auto [sid, s] : w.stations)
            if (w.towns.contains(s.town)) byTown[s.town.value].push_back(sid);
        for (const auto& [from, fromStations] : byTown)
            for (const auto& [to, toStations] : byTown) {
                if (from == to) continue;
                TownLink link{TownId(to), {}, {}};
                for (const StationId a : fromStations)
                    for (const StationId b : toStations) {
                        if (const auto* route = w.routing.find(a, b); !route || !route->hasRide) continue;
                        if (std::ranges::find(link.origins, a) == link.origins.end())
                            link.origins.push_back(a);
                        if (std::ranges::find(link.destinations, b) == link.destinations.end())
                            link.destinations.push_back(b);
                    }
                if (!link.origins.empty()) links.byTown[from].push_back(std::move(link));
            }
    }
}

void schedule(World& w, PersonId id, i64 tick) {
    Person& p = w.people[id];
    p.nextEvent = tick;
    w.personEvents.push(PersonEvent{tick, id.value, p.serial});
}

void process(World& w) {
    while (!w.personEvents.empty() && w.personEvents.top().tick <= w.clock.tick) {
        const PersonEvent ev = w.personEvents.top();
        w.personEvents.pop();
        const PersonId id(ev.id);
        const Person* p = w.people.tryGet(id);
        if (!p || p->serial != ev.serial || p->nextEvent != ev.tick) continue; // stale
        handleEvent(w, id);
    }
}

void reschedule(World& w, PersonId id) {
    if (w.people[id].state == PersonState::AtHome) scheduleFromHome(w, id);
}

void assignJob(World& w, PersonId id) {
    const TownId town = w.people[id].town;
    if (findVacancy(w, town, id)) return;
    if (Town* t = w.towns.tryGet(town)) t->unemployed.push_back(id);
}

void assignShop(World& w, PersonId id) {
    Person& p = w.people[id];
    if (Building* old = resolve(w, p.shop); old && old->customers > 0) --old->customers;
    p.shop = {};
    const Town* home = w.towns.tryGet(p.town);
    if (!home) return;
    TownId target = w.towns.contains(home->shopTown) ? home->shopTown : p.town;
    const Town* st = w.towns.tryGet(target);
    if (!st || st->retail.empty()) {
        st = home;
        if (st->retail.empty()) return;
    }
    // Weighted pick by capacity, deterministic per person.
    i64 total = 0;
    for (const BuildingId b : st->retail)
        if (const Building* bd = w.buildings.tryGet(b)) total += bd->info().customers;
    if (total <= 0) return;
    i64 pick = static_cast<i64>(mix32(p.serial * 0x9E3779B9u) % static_cast<u64>(total));
    for (const BuildingId b : st->retail) {
        Building* bd = w.buildings.tryGet(b);
        if (!bd) continue;
        if (pick < bd->info().customers) {
            p.shop = bd->ref(b);
            ++bd->customers;
            return;
        }
        pick -= bd->info().customers;
    }
}

void jobMarket(World& w) {
    for (auto [tid, t] : w.towns) {
        if (t.unemployed.empty()) continue;
        struct Candidate {
            bool linked; // reachable by public transport: commuters take the train or bus
            i64 dist;
            TownId id;
        };
        const std::vector<TownLink>& links = linkedTowns(w, tid);
        std::vector<Candidate> candidates;
        for (const auto [oid, o] : w.towns) {
            if (oid == tid || o.jobs <= o.workers) continue;
            const i64 d = chebyshev(o.centre.x - t.centre.x, o.centre.y - t.centre.y);
            const bool linked = std::ranges::any_of(links, [&](const TownLink& l) { return l.town == oid; });
            if (d <= 160 || (linked && d <= 400)) candidates.push_back({linked, d, oid});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.linked != b.linked) return a.linked;
            return a.dist != b.dist ? a.dist < b.dist : a.id < b.id;
        });
        std::vector<PersonId> still;
        const std::vector<PersonId> list = t.unemployed;
        for (const PersonId pid : list) {
            const Person* p = w.people.tryGet(pid);
            if (!p || p->employed() || p->town != tid) continue;
            bool found = findVacancy(w, tid, pid);
            for (usize i = 0; !found && i < candidates.size(); ++i) found = findVacancy(w, candidates[i].id, pid);
            if (found)
                reschedule(w, pid);
            else
                still.push_back(pid);
        }
        w.towns[tid].unemployed = std::move(still);
    }
}

void rebuildEvents(World& w) {
    privatecars::rebuild(w);
    w.personEvents = {};
    for (const auto [id, p] : w.people)
        if (p.nextEvent >= 0) w.personEvents.push(PersonEvent{p.nextEvent, id.value, p.serial});
}

std::string_view stateName(PersonState s) {
    switch (s) {
    case PersonState::AtHome: return "At home";
    case PersonState::AtWork: return "At work";
    case PersonState::AtShop:
        return "Shopping or visiting";
    case PersonState::Walking: return "Walking";
    case PersonState::Driving: return "Driving";
    case PersonState::Lift: return "Getting a lift";
    case PersonState::ToStop: return "Walking to a stop";
    case PersonState::Waiting: return "Waiting for transport";
    case PersonState::Riding: return "On board";
    case PersonState::FromStop: return "Walking from a stop";
    }
    return "?";
}

} // namespace tp::people
