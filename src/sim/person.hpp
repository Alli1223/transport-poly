#pragma once
// Individually simulated citizens. Each person lives in a home, may work somewhere and shops in a
// favourite retail building. Their day is driven by scheduled events, so idle people cost nothing.

#include "core/calendar.hpp"
#include "core/types.hpp"
#include "sim/buildings.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"
#include "sim/walking.hpp"

#include <compare>
#include <unordered_map>
#include <vector>

namespace tp {

class World;

enum class PersonState : u8 {
    AtHome,
    AtWork,
    AtShop,
    Walking, // on foot to the destination
    Driving, // in their own car (abstract)
    Lift,    // no car and no usable transit: someone drives them (abstract, unserved demand)
    ToStop,  // walking to a station
    Waiting, // waiting at a station
    Riding,  // on board a vehicle
    FromStop // walking from a station to the destination
};

// Visit: a day out to another town the person can reach by public transport, on business or to
// shop and look around.
enum class TripPurpose : u8 { None, Work, Shop, Home, Visit };

struct Person {
    u32 serial = 0;
    BuildingRef home;
    BuildingRef work;
    BuildingRef shop;
    BuildingRef visit; // destination of the planned or current visit to another town
    TownId town;
    PersonState state = PersonState::AtHome;
    TripPurpose purpose = TripPurpose::None;
    bool carOwner = false;
    u16 departMinute = 7 * 60; // usual weekday departure (adapts to the commute duration)
    u16 workMinutes = 8 * 60;
    u16 arriveMinute = 8 * 60 + 30; // preferred arrival time at work
    u16 commuteMinutes = 30;        // duration of the last commute
    i64 nextEvent = -1;        // tick of the next scheduled state change, -1 = none
    i64 tripStart = 0;
    TilePos tripFrom2;         // trip end points in half tiles (for rendering pedestrians)
    TilePos tripTo2;
    StationId station;         // current / boarding station while using transit
    StationId targetStation;   // station to alight at for the current leg
    StationId finalStation;    // station closest to the destination
    VehicleId vehicle;
    i64 boardTick = 0;         // queue arrival while waiting, ride start while aboard
    u32 nameSeed = 0;
    // Physical boarding / alighting, retained after leaving the vehicle so every
    // person walks from their own door instead of appearing at the station sign.
    TilePos transitFrom2, transitTo2;
    i64 transitBegin = -1, transitEnd = -1;
    u8 transitMotion = 0; // 0 none, 1 boarding, 2 alighting

    std::vector<WalkPoint> walkPath;
    i64 walkBegin = 0, walkEnd = 0;

    bool employed() const { return work.valid(); }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(serial, home, work, shop, town, state, purpose, carOwner, departMinute, workMinutes, arriveMinute,
           commuteMinutes, nextEvent, tripStart,
           tripFrom2, tripTo2, station, targetStation, finalStation, vehicle, boardTick, nameSeed);
        if (ar.version == 0 || ar.version >= 18) ar(walkPath,walkBegin,walkEnd);
        if (ar.version == 0 || ar.version >= 11) ar(transitFrom2, transitTo2, transitBegin, transitEnd, transitMotion);
        if (ar.version == 0 || ar.version >= 22) ar(visit);
    }
};

// A town linked to another by public transport: the stations of the first with routes to the
// second, and the stations of the second they reach.
struct TownLink {
    TownId town;
    std::vector<StationId> origins;
    std::vector<StationId> destinations;
};

// Derived (never saved): towns linked by public transport, rebuilt when the routing changes.
struct TownLinks {
    i64 routing = -2; // RoutingTable::builtTick these links were made from
    std::unordered_map<u32, std::vector<TownLink>> byTown;
};

struct PersonEvent {
    i64 tick = 0;
    u32 id = 0;
    u32 serial = 0;
    friend constexpr auto operator<=>(const PersonEvent&, const PersonEvent&) = default;
};

// Travel speeds in ticks per tile (a tick is 15 game seconds).
inline constexpr i64 kWalkTicksPerTile = 30;  // ~1 tile per real second at normal speed
inline constexpr i64 kDriveTicksPerTile = 6;  // about as fast as a bus, without stops
inline constexpr i64 kDriveOverheadTicks = 15 * kTicksPerMinute;
inline constexpr i64 kLiftTicksPerTile = 8;
inline constexpr i64 kLiftOverheadTicks = 30 * kTicksPerMinute;
inline constexpr i32 kMaxWalkTiles = 7;

namespace people {

PersonId create(World& w, BuildingId home);
void remove(World& w, PersonId id);
void schedule(World& w, PersonId id, i64 tick);
// Travels without public transport (walk / drive / lift) between two half-tile positions.
void travelAbstract(World& w, PersonId id, TilePos from2, TilePos to2, bool newTrip);
// Runs all events due at the current tick.
void process(World& w);
void assignJob(World& w, PersonId id);
// Re-plans the next departure of a person who is at home (e.g. after finding a job).
void reschedule(World& w, PersonId id);
void assignShop(World& w, PersonId id);
// Matches unemployed people with vacancies in nearby towns (towns linked by public transport
// first, and further away).
void jobMarket(World& w);
// The towns a town is linked to by public transport.
const std::vector<TownLink>& linkedTowns(World& w, TownId town);
// Rebuilds the event queue from the people pool (after loading a game).
void rebuildEvents(World& w);
std::string_view stateName(PersonState s);

} // namespace people

} // namespace tp
