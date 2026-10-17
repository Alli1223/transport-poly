#pragma once
// Public transport for citizens: the link graph formed by vehicle orders, station-to-station
// routing tables, boarding / alighting and fares.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

#include <unordered_map>
#include <vector>

namespace tp {

class World;

struct RouteEntry {
    StationId nextHop;
    i32 cost = 0; // expected ticks (waiting + riding) to reach the destination
};

// Derived routing data (rebuilt from orders; never saved).
struct RoutingTable {
    // origin station -> destination station -> route
    std::unordered_map<u32, std::unordered_map<u32, RouteEntry>> routes;
    bool dirty = true;
    i64 builtTick = -1;

    const RouteEntry* find(StationId from, StationId to) const;
};

namespace passengers {

// Rebuilds routing tables when the network changed (called every tick, cheap when clean).
void updateRouting(World& w);
void markDirty(World& w);

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

// A vehicle stopped at a station: passengers for this station get off, waiting passengers whose
// next hop is the vehicle's next stop get on. Returns the number of people moved.
i32 exchangeAtStop(World& w, VehicleId vehicle, StationId station);
// The vehicle a person rides disappeared: send them on their way.
void abandonRide(World& w, PersonId person);
// A person walking to a station arrived there.
void arriveAtStation(World& w, PersonId person);
// A person waited too long: they leave the station and find another way.
void giveUpWaiting(World& w, PersonId person);
// Fare for a journey of `tiles` taking `ticks`.
Money fare(i64 tiles, i64 ticks);

} // namespace passengers

} // namespace tp
