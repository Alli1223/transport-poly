#pragma once
// Vehicles: the model catalogue, orders and per-vehicle state.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tp {

class World;

enum class VehicleType : u8 { Road, Tram, Rail, Ship };

struct VehicleModel {
    std::string_view name;
    VehicleType type;
    u16 capacity;    // passengers
    u16 maxSpeedKmh; // displayed speed
    u16 length;      // in 1/16 tile
    Money price;
    Money runningCost; // per year
    i32 introYear;
    i32 lifeYears;
    u8 visual;       // rendering model variant
    bool powered = false; // trains: locomotives and multiple units can lead a train
};

std::span<const VehicleModel> vehicleModels();
const VehicleModel& vehicleModel(u16 index);
std::string_view vehicleTypeName(VehicleType t);

// Reliability in basis points (10000 = 100%): newer designs are more reliable.
constexpr u16 maxReliability(const VehicleModel& m) {
    return static_cast<u16>(std::clamp(7000 + (m.introYear - 1920) * 40, 7000, 9600));
}
// Days between services before a vehicle heads for its depot on its own.
constexpr i32 serviceIntervalDays(VehicleType t) {
    switch (t) {
    case VehicleType::Road:
    case VehicleType::Tram: return 120;
    case VehicleType::Rail: return 180;
    case VehicleType::Ship: return 240;
    }
    return 150;
}

// Simulation speed units: 1 tile = 256 units of progress. A 56 km/h bus covers ~0.18 tiles per
// tick (about five tiles per real second at normal speed).
inline constexpr i32 kTileUnits = 256;
constexpr i32 speedUnits(i32 kmh) { return (kmh * 80 + 50) / 100; } // units per tick

enum OrderFlags : u8 {
    kOrderNonStop = 1 << 0,
    kOrderFullLoad = 1 << 1,
    kOrderUnloadOnly = 1 << 2,
    kOrderNoLoading = 1 << 3,
};

enum class OrderKind : u8 { Station, Depot, Via };

struct Order {
    OrderKind kind = OrderKind::Station;
    StationId station;
    DepotId depot;
    u8 flags = 0;
    TilePos waypoint;

    bool passThrough() const { return kind == OrderKind::Via || (kind == OrderKind::Station && (flags & kOrderNonStop)); }

    friend bool operator==(const Order&, const Order&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(kind, station, depot, flags);
        if (ar.version == 0 || ar.version >= 9) ar(waypoint);
    }
};

// One track piece a train occupies or has reserved: the tile, the edge it is entered through and
// the edge it is left through.
struct TrackSeg {
    TilePos tile;
    Dir entry = Dir::S;
    Dir exit = Dir::N;

    friend bool operator==(const TrackSeg&, const TrackSeg&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tile, entry, exit);
    }
};

enum class VehicleState : u8 {
    InDepot, // parked inside its depot
    Driving, // travelling towards the current order target
    Loading, // stopped at a station
};

struct Vehicle {
    u32 serial = 0;
    CompanyId owner;
    u16 model = 0;
    u16 unitNumber = 0;
    VehicleState state = VehicleState::InDepot;
    bool stopped = true; // the player's start / stop switch
    bool sendToDepot = false;
    bool lost = false;
    DepotId depot; // depot it was bought in / last visited

    // Position on the road network: the tile, the edge it entered through, the edge it leaves
    // through and the progress along that in-tile path.
    TilePos tile;
    Dir entry = Dir::S;
    Dir exit = Dir::N;
    i32 progress = 0;
    i32 speed = 0; // current speed, units per tick
    bool stopAtMiddle = false;
    i32 blockedTicks = 0; // ticks spent waiting behind another vehicle (gridlock breaker)

    std::vector<u8> path; // planned exits (Dir) for the tiles after the current one
    u32 pathIndex = 0;
    i32 tilesSincePlan = 0;

    std::vector<Order> orders;
    u8 orderIndex = 0;

    std::vector<PersonId> passengers;
    i64 loadingUntil = 0;

    // Trains only.
    std::vector<u16> cars;       // consist, cars[0] leads (its model is also `model`)
    std::deque<TrackSeg> trail;  // pieces under the train, head first
    std::deque<TrackSeg> ahead;  // pieces reserved in front of the head, in driving order
    i32 retryTicks = 0;

    // Ships only: sailing from the centre of `tile` towards the neighbour in direction `heading`
    // (eight way, see water_path.hpp) while `moving`; `path` then holds the headings that follow.
    u8 heading = 0;
    u8 prevHeading = 0;
    bool moving = false;

    // Reliability (basis points), servicing and breakdowns.
    u16 reliability = 10000;
    i64 lastServiceTick = 0;
    i32 breakdownTicks = 0; // ticks left standing broken down
    u8 breakdownsSinceService = 0;
    bool goingForService = false; // heading for its depot for a service; keeps its place in the orders
    u32 orderGroup = 0;           // vehicles with the same non-zero group share one order list

    Money profitThisYear = 0;
    Money profitLastYear = 0;
    Money value = 0;
    i64 builtTick = 0;
    i32 passengersThisYear = 0;

    const VehicleModel& info() const { return vehicleModel(model); }
    // Seats in the whole vehicle (all cars of a train).
    u32 capacity() const {
        if (cars.empty()) return info().capacity;
        u32 c = 0;
        for (const u16 m : cars) c += vehicleModel(m).capacity;
        return c;
    }
    Money runningCost() const {
        if (cars.empty()) return info().runningCost;
        Money c = 0;
        for (const u16 m : cars) c += vehicleModel(m).runningCost;
        return c;
    }
    const Order* currentOrder() const { return orders.empty() ? nullptr : &orders[orderIndex % orders.size()]; }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(serial, owner, model, unitNumber, state, stopped, sendToDepot, lost, depot, tile, entry, exit, progress,
           speed, stopAtMiddle, blockedTicks, path, pathIndex, tilesSincePlan, orders, orderIndex, passengers,
           loadingUntil, cars, trail, ahead, retryTicks, profitThisYear, profitLastYear, value, builtTick,
           passengersThisYear);
        if (ar.version == 0 || ar.version >= 2) ar(heading, prevHeading, moving);
        if (ar.version == 0 || ar.version >= 4)
            ar(reliability, lastServiceTick, breakdownTicks, breakdownsSinceService, goingForService, orderGroup);
    }
};

// The order a vehicle is heading for: "send to depot" and service visits override the order list.
inline std::optional<Order> targetOrder(const Vehicle& v) {
    if (v.sendToDepot || v.goingForService) {
        Order o;
        o.kind = OrderKind::Depot;
        o.depot = v.depot;
        return o;
    }
    if (const Order* o = v.currentOrder()) return *o;
    return std::nullopt;
}

// In-tile path lengths (units) for road vehicles driving on the right.
inline constexpr i32 kPathStraight = 256;
inline constexpr i32 kPathRightTurn = 121; // inner curve
inline constexpr i32 kPathLeftTurn = 281;  // outer curve
inline constexpr i32 kPathUTurn = 341;

enum class TurnKind : u8 { Straight, Right, Left, UTurn };

constexpr TurnKind turnKind(Dir entry, Dir exit) {
    const Dir travel = opposite(entry);
    if (exit == travel) return TurnKind::Straight;
    if (exit == entry) return TurnKind::UTurn;
    if (exit == rotateCW(travel)) return TurnKind::Right;
    return TurnKind::Left;
}

constexpr i32 pathLength(Dir entry, Dir exit) {
    switch (turnKind(entry, exit)) {
    case TurnKind::Straight: return kPathStraight;
    case TurnKind::Right: return kPathRightTurn;
    case TurnKind::Left: return kPathLeftTurn;
    case TurnKind::UTurn: return kPathUTurn;
    }
    return kPathStraight;
}

namespace trains {

void tick(World& w, VehicleId id);
// Tries to leave the depot; returns true once the train is out on the track.
bool leaveDepot(World& w, VehicleId id);
// Releases every track reservation held by the train.
void releaseAll(World& w, VehicleId id);
i32 consistLength(const Vehicle& v); // in progress units
i32 maxSpeed(const Vehicle& v);      // units per tick
// Limit for curves under the consist and braking for curves on reserved track ahead.
i32 trackSpeedLimit(const Vehicle& v);

} // namespace trains

namespace ships {

void tick(World& w, VehicleId id);
// Tries to leave the shipyard; returns true once the ship is on the water.
bool leaveDepot(World& w, VehicleId id);
// The water tile ships moor at in front of a dock tile.
TilePos berth(const World& w, TilePos dock);

} // namespace ships

namespace vehicles {

VehicleId buy(World& w, CompanyId owner, DepotId depot, u16 model);
void sell(World& w, VehicleId id);
void start(World& w, VehicleId id);
// Called every tick for every vehicle.
void tickAll(World& w);
void daily(World& w);
void yearly(World& w);
// Removes a passenger from a vehicle (e.g. because the person is deleted).
void dropPassengers(World& w, VehicleId id);
// Marks every vehicle's path as stale (after the road network changed).
void invalidatePaths(World& w);
// A vehicle has just entered a depot: service it, then stop it there (after "send to depot"),
// let it carry on with its orders (after a service visit) or move on to the next order.
void arriveInDepot(World& w, VehicleId id);
i32 ageDays(const World& w, const Vehicle& v);

} // namespace vehicles

} // namespace tp
