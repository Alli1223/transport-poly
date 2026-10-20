#pragma once
// Public transport for citizens: the link graph formed by vehicle orders, station-to-station
// routing tables, boarding / alighting and fares.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"
#include "sim/walking.hpp"
#include <map>

#include <unordered_map>
#include <vector>

namespace tp {

class World;

struct RouteEntry {
    StationId nextHop;
    i32 cost = 0; // expected ticks (waiting + riding) to reach the destination
    bool walk = false; // the first leg is a pedestrian transfer to nextHop
    bool hasRide = true;
    friend bool operator==(const RouteEntry&, const RouteEntry&) = default;
};

// Derived routing data (rebuilt from orders; never saved).
struct RoutingTable {
    // origin station -> destination station -> route
    std::unordered_map<u32, std::unordered_map<u32, RouteEntry>> routes;
    std::map<std::pair<u32, u32>, std::vector<WalkPoint>> transfers;
    bool dirty = true;
    bool preserveSchedulesOnBuild = false; // first rebuild after loading keeps saved departures
    i64 builtTick = -1;

    const RouteEntry* find(StationId from, StationId to) const;
};

namespace passengers {

// Rebuilds routing tables when the network changed (called every tick, cheap when clean).
void updateRouting(World& w);
void markDirty(World& w);
// Assign rail queues to departure platforms and spread passengers along each platform.
void positionWaiting(World& w, StationId station);
void updateWaiting(World& w);

// Plans a public transport trip; returns the expected duration (ticks) and fills the stations,
// or -1 when no route exists.
struct TransitPlan {
    StationId origin;
    StationId nextHop;
    StationId destination;
    i64 walkToStop = 0;
    i64 total = 0;
};
bool planTrip(const World& w, TilePos from2, TilePos to2, TransitPlan& out);
// Local stops plus rail stations serving the endpoint building's town. Walking routes are
// checked by planTrip, so town-wide rail access still requires connected streets and entrances.
void accessStations(const World& w, TilePos centre2, std::vector<StationId>& out);

// A vehicle stopped at a station: passengers for this station get off, waiting passengers whose
// next hop is the vehicle's next stop get on. Returns the number of people moved.
i32 exchangeAtStop(World& w, VehicleId vehicle, StationId station);
// The vehicle a person rides disappeared: send them on their way.
void abandonRide(World& w, PersonId person);
// A person walking to a station arrived there.
void arriveAtStation(World& w, PersonId person);
// Keep waiting while a service exists; re-route or leave only if the service disappears.
void reviewWaiting(World& w, PersonId person);
// Force a passenger off a closed station (e.g. company liquidation).
void giveUpWaiting(World& w, PersonId person);
// Fare for a journey of `tiles` taking `ticks`.
Money fare(i64 tiles, i64 ticks);

} // namespace passengers

} // namespace tp
