#pragma once
// Individually simulated citizens. Each person lives in a home, may work somewhere and shops in a
// favourite retail building. Their day is driven by scheduled events, so idle people cost nothing.

#include "core/calendar.hpp"
#include "core/types.hpp"
#include "sim/buildings.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

#include <compare>

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

enum class TripPurpose : u8 { None, Work, Shop, Home };

struct Person {
    u32 serial = 0;
    BuildingRef home;
    BuildingRef work;
    BuildingRef shop;
    TownId town;
    PersonState state = PersonState::AtHome;
    TripPurpose purpose = TripPurpose::None;
    bool carOwner = false;
    u16 departMinute = 7 * 60; // usual weekday departure
    u16 workMinutes = 8 * 60;
    i64 nextEvent = -1;        // tick of the next scheduled state change, -1 = none
    i64 tripStart = 0;
    TilePos tripFrom2;         // trip end points in half tiles (for rendering pedestrians)
    TilePos tripTo2;
    StationId station;         // current / boarding station while using transit
    StationId targetStation;   // station to alight at for the current leg
    StationId finalStation;    // station closest to the destination
    VehicleId vehicle;
    u32 nameSeed = 0;

    bool employed() const { return work.valid(); }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(serial, home, work, shop, town, state, purpose, carOwner, departMinute, workMinutes, nextEvent, tripStart,
           tripFrom2, tripTo2, station, targetStation, finalStation, vehicle, nameSeed);
    }
};

struct PersonEvent {
    i64 tick = 0;
    u32 id = 0;
    u32 serial = 0;
    friend constexpr auto operator<=>(const PersonEvent&, const PersonEvent&) = default;
};

// Travel speeds in ticks per tile (a tick is 15 game seconds).
inline constexpr i64 kWalkTicksPerTile = 30;  // ~1 tile per real second at normal speed
inline constexpr i64 kDriveTicksPerTile = 10;
inline constexpr i64 kDriveOverheadTicks = 15 * kTicksPerMinute;
inline constexpr i64 kLiftTicksPerTile = 12;
inline constexpr i64 kLiftOverheadTicks = 30 * kTicksPerMinute;
inline constexpr i32 kMaxWalkTiles = 7;

namespace people {

PersonId create(World& w, BuildingId home);
void remove(World& w, PersonId id);
void schedule(World& w, PersonId id, i64 tick);
// Runs all events due at the current tick.
void process(World& w);
void assignJob(World& w, PersonId id);
void assignShop(World& w, PersonId id);
// Matches unemployed people with vacancies in nearby towns.
void jobMarket(World& w);
// Rebuilds the event queue from the people pool (after loading a game).
void rebuildEvents(World& w);
std::string_view stateName(PersonState s);

} // namespace people

} // namespace tp
