#include "sim/person.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/passengers.hpp"
#include "sim/world.hpp"

#include <algorithm>

namespace tp::people {

namespace {

i32 carOwnershipPercent(i32 year) { return iclamp(12 + (year - 1950) * 6 / 5, 12, 75); }

Building* resolve(World& w, const BuildingRef& r) {
    Building* b = w.buildings.tryGet(r.id);
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

void scheduleFromHome(World& w, PersonId id) {
    Person& p = w.people[id];
    const i64 now = w.clock.tick;
    const i32 today = w.clock.day();
    for (i32 d = today; d < today + 8; ++d) {
        const i64 dayStart = w.clock.tickOfDay(d);
        if (!isWeekend(weekdayFromDays(d))) {
            if (p.employed()) {
                const i64 jitter = static_cast<i64>(mix32(p.serial * 31u + static_cast<u32>(d)) % 21) - 10;
                const i64 t = dayStart + (p.departMinute + jitter) * kTicksPerMinute;
                if (t > now) {
                    p.purpose = TripPurpose::Work;
                    schedule(w, id, t);
                    return;
                }
            }
        } else if (p.shop.valid()) {
            const i64 minute = 9 * 60 + mix32(p.serial ^ (static_cast<u32>(d) * 2654435761u)) % 300;
            const i64 t = dayStart + minute * kTicksPerMinute;
            if (t > now) {
                p.purpose = TripPurpose::Shop;
                schedule(w, id, t);
                return;
            }
        }
    }
    p.purpose = TripPurpose::None;
    schedule(w, id, w.clock.tickOfDay(today + 1) + 8 * kTicksPerHour);
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
            const i64 car = dist * kDriveTicksPerTile + kDriveOverheadTicks;
            // People without a car use public transport whenever a reasonable route exists; car
            // owners only switch when it is within 1.5x of the driving time.
            const bool take = p.carOwner ? plan.total * 10 < car * 15 : plan.total < 6 * kTicksPerHour;
            (void)car;
            if (take) {
                p.state = PersonState::ToStop;
                p.station = plan.origin;
                p.targetStation = plan.nextHop;
                p.finalStation = plan.destination;
                if (town) ++town->thisMonth.transit;
                schedule(w, id, w.clock.tick + std::max<i64>(1, plan.walkToStop));
                return;
            }
        }
    }
    travelAbstract(w, id, from2, to2, false);
}

void arrive(World& w, PersonId id) {
    Person& p = w.people[id];
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
    case TripPurpose::Home:
    case TripPurpose::None:
        p.state = PersonState::AtHome;
        scheduleFromHome(w, id);
        break;
    }
}

void handleEvent(World& w, PersonId id);

} // namespace

void travelAbstract(World& w, PersonId id, TilePos from2, TilePos to2, bool newTrip) {
    Person& p = w.people[id];
    const i64 dist = distanceTiles(from2, to2);
    Town* town = w.towns.tryGet(p.town);
    if (newTrip) {
        p.tripStart = w.clock.tick;
        if (town) ++town->thisMonth.trips;
    }
    p.tripFrom2 = from2;
    p.tripTo2 = to2;
    i64 duration;
    if (dist <= kMaxWalkTiles) {
        p.state = PersonState::Walking;
        duration = std::max<i64>(1, dist) * kWalkTicksPerTile;
        if (town) ++town->thisMonth.walked;
    } else if (p.carOwner) {
        p.state = PersonState::Driving;
        duration = dist * kDriveTicksPerTile + kDriveOverheadTicks;
        if (town) ++town->thisMonth.drove;
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
        Building* dest = resolve(w, p.purpose == TripPurpose::Work ? p.work : p.shop);
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
    case PersonState::Driving:
    case PersonState::Lift:
    case PersonState::FromStop:
        arrive(w, id);
        return;
    case PersonState::ToStop:
        passengers::arriveAtStation(w, id);
        return;
    case PersonState::Waiting:
        passengers::giveUpWaiting(w, id);
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
        p.carOwner = rng.chance(static_cast<u32>(carOwnershipPercent(w.clock.date().year)), 100);
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
    if (p.state == PersonState::Waiting) {
        if (Station* s = w.stations.tryGet(p.station)) {
            auto it = s->waiting.find(p.targetStation);
            if (it != s->waiting.end()) {
                std::erase(it->second, id);
                if (it->second.empty()) s->waiting.erase(it);
            }
        }
    } else if (p.state == PersonState::Riding) {
        if (Vehicle* v = w.vehicles.tryGet(p.vehicle)) std::erase(v->passengers, id);
    }
    if (Building* b = resolve(w, p.home); b && b->residents > 0) --b->residents;
    if (Building* b = resolve(w, p.work); b && b->workers > 0) {
        --b->workers;
        if (Town* t = w.towns.tryGet(b->town); t && t->workers > 0) --t->workers;
    }
    if (Building* b = resolve(w, p.shop); b && b->customers > 0) --b->customers;
    w.people.destroy(id);
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
            i64 dist;
            TownId id;
        };
        std::vector<Candidate> candidates;
        for (const auto [oid, o] : w.towns) {
            if (oid == tid || o.jobs <= o.workers) continue;
            const i64 d = chebyshev(o.centre.x - t.centre.x, o.centre.y - t.centre.y);
            if (d <= 160) candidates.push_back({d, oid});
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.dist != b.dist ? a.dist < b.dist : a.id < b.id; });
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
    w.personEvents = {};
    for (const auto [id, p] : w.people)
        if (p.nextEvent >= 0) w.personEvents.push(PersonEvent{p.nextEvent, id.value, p.serial});
}

std::string_view stateName(PersonState s) {
    switch (s) {
    case PersonState::AtHome: return "At home";
    case PersonState::AtWork: return "At work";
    case PersonState::AtShop: return "Shopping";
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
