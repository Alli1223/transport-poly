#pragma once
// Vehicles: the model catalogue, orders and per-vehicle state.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/cargo.hpp"
#include "sim/ids.hpp"
#include "sim/track.hpp"

#include <algorithm>
#include <array>
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
    CargoType cargoType = CargoType::None;
    u16 cargoCapacity = 0;
    bool openCargo = false;
    bool electric = false; // trains: an engine that runs only under overhead wires
    bool intercity = false; // road: a long distance coach rather than a town bus
};

std::span<const VehicleModel> vehicleModels();
const VehicleModel& vehicleModel(u16 index);
std::string_view vehicleTypeName(VehicleType t);
// A model can take another's place in a fleet: the same kind of vehicle (engine, coach, wagon,
// bus or lorry) carrying the same class of load.
bool canReplace(const VehicleModel& from, const VehicleModel& to);

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
    kOrderPlatform = 1 << 4, // free-form track: stop at the platform nearest Order::platform
    kOrderTimingPoint = 1 << 5, // hold here so the vehicles running this service leave evenly spaced
};

// Longest timetabled stop or departure interval, in minutes.
inline constexpr u16 kMaxTimetableMinutes = 24 * 60;

enum class OrderKind : u8 { Station, Depot, Via };

struct Order {
    OrderKind kind = OrderKind::Station;
    StationId station;
    DepotId depot;
    u8 flags = 0;
    TilePos waypoint;
    TPoint platform; // with kOrderPlatform: a point on the chosen platform
    // Timetable: stop at least this long, and with kOrderTimingPoint leave this long after the
    // previous vehicle of the service (0: the round trip shared out between its vehicles).
    u16 waitMinutes = 0;
    u16 intervalMinutes = 0;
    // Freight at this stop, a bit per cargo type (0: every kind): what may be picked up here and
    // what is dropped off here, delivered or left for another service.
    u32 loadCargo = 0;
    u32 unloadCargo = 0;

    bool passThrough() const { return kind == OrderKind::Via || (kind == OrderKind::Station && (flags & kOrderNonStop)); }
    bool loads(CargoType t) const {
        return !passThrough() && !(flags & (kOrderNoLoading | kOrderUnloadOnly)) && (!loadCargo || (loadCargo >> static_cast<u32>(t) & 1));
    }
    bool unloads(CargoType t) const { return !unloadCargo || (unloadCargo >> static_cast<u32>(t) & 1); }

    friend bool operator==(const Order&, const Order&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(kind, station, depot, flags);
        if (ar.version == 0 || ar.version >= 9) ar(waypoint);
        if (ar.version == 0 || ar.version >= 16) ar(platform);
        if (ar.version == 0 || ar.version >= 29) ar(waitMinutes, intervalMinutes);
        if (ar.version == 0 || ar.version >= 31) ar(loadCargo, unloadCargo);
    }
};
inline constexpr u32 kAllCargo = (1u << kCargoTypes) - 1;

// A line made in the lines window: a named list of orders that its vehicles share (their order
// group is the line's id). It keeps its stops while no vehicle runs it.
struct ServiceLine {
    CompanyId owner;
    VehicleType type = VehicleType::Rail;
    std::string name;
    std::vector<Order> orders;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(owner, type, name, orders);
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
    i32 speedSub = 0; // trains: the part of a unit per tick above `speed`, in 1/256
    bool stopAtMiddle = false;
    i32 blockedTicks = 0; // ticks spent waiting behind another vehicle (gridlock breaker)

    std::vector<u8> path; // planned exits (Dir) for the tiles after the current one
    u32 pathIndex = 0;
    i32 tilesSincePlan = 0;

    std::vector<Order> orders;
    u8 orderIndex = 0;

    std::vector<PersonId> passengers;
    std::vector<u16> passengerSeats; // stable seat numbers parallel to the passenger manifest
    std::vector<CargoBatch> cargo;
    i64 cargoExchangeTick = -1;
    u32 cargoExchangeUnits = 0;
    CargoCounts cargoLoaded, cargoUnloaded;
    i64 loadingUntil = 0;
    i64 loadingStartTick = 0;

    // Trains only.
    std::vector<u16> cars;       // physical order from front to rear; `model` identifies a powered unit anywhere in it
    std::deque<TrackSeg> trail;  // pieces under the train, head first
    std::deque<TrackSeg> ahead;  // pieces reserved in front of the head, in driving order
    i32 retryTicks = 0;

    // Ships only: sailing from the centre of `tile` towards the neighbour in direction `heading`
    // (eight way, see water_path.hpp) while `moving`; `path` then holds the headings that follow.
    u8 heading = 0;
    u8 prevHeading = 0;
    bool moving = false;

    // Trains on free-form track: the edges under the train (head first) and those reserved
    // ahead; `progress` is then the distance along the head's edge.
    bool freeTrack = false;
    std::deque<EdgeRun> runs;
    std::deque<EdgeRun> runsAhead;

    // Reliability (basis points), servicing and breakdowns.
    u16 reliability = 10000;
    i64 lastServiceTick = 0;
    i32 breakdownTicks = 0; // ticks left standing broken down
    u8 breakdownsSinceService = 0;
    bool goingForService = false; // heading for its depot for a service; keeps its place in the orders
    u32 orderGroup = 0;           // vehicles with the same non-zero group share one order list

    // Timetable: when it last left each order's stop (timing points only, -1 never), when it
    // started holding at the current timing point (-1 not holding), and its round trip from the
    // first timing point back to it, without the time spent holding.
    std::vector<i64> departures;
    i64 holdingSince = -1;
    i64 heldTicks = 0;
    i64 cycleTicks = 0;
    // Passenger satisfaction (see feedback.hpp): ride scores out of 100 added up, and how many,
    // this month and last.
    std::array<i64, 2> satisfactionSum{};
    std::array<i32, 2> satisfactionCount{};

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
    // Space for a cargo: every car of the cargo's class (hopper, refrigerated, box, flatbed)
    // counts, and everything of that class aboard shares it.
    u32 cargoCapacity(CargoType type) const {
        const CargoClass k = cargoClass(type);
        const auto fits = [&](const VehicleModel& m) { return k != CargoClass::None && m.cargoType != CargoType::None && cargoClass(m.cargoType) == k; };
        if (cars.empty()) return fits(info()) ? info().cargoCapacity : 0;
        u32 n = 0;
        for (const auto m : cars) if (fits(vehicleModel(m))) n += vehicleModel(m).cargoCapacity;
        return n;
    }
    u16 passengerSeat(usize manifestIndex) const {
        return passengerSeats.size() == passengers.size() ? passengerSeats[manifestIndex] : static_cast<u16>(manifestIndex);
    }
    u32 carLoad(usize index) const {
        const auto& m = vehicleModel(cars.empty() ? model : cars[index]);
        const bool seats = m.cargoType == CargoType::None;
        const CargoClass k = seats ? CargoClass::None : cargoClass(m.cargoType);
        u32 before = 0;
        for (usize k2 = 0; k2 < index && k2 < cars.size(); ++k2) {
            const auto& prev = vehicleModel(cars[k2]);
            before += seats ? prev.capacity : prev.cargoType != CargoType::None && cargoClass(prev.cargoType) == k ? prev.cargoCapacity : 0;
        }
        if (seats && passengerSeats.size() == passengers.size()) {
            return static_cast<u32>(std::ranges::count_if(passengerSeats, [&](u16 seat) {
                return seat >= before && seat < before + m.capacity;
            }));
        }
        const u32 total = seats ? static_cast<u32>(passengers.size()) : cargo::classQuantity(cargo, k);
        return std::min<u32>(total > before ? total - before : 0, seats ? m.capacity : m.cargoCapacity);
    }
    // An electric train needs overhead wires all the way: every engine in it is electric.
    bool needsWires() const {
        if (cars.empty()) return info().electric;
        bool any = false;
        for (const u16 m : cars) {
            const VehicleModel& car = vehicleModel(m);
            if (car.powered && !car.electric) return false;
            any = any || car.powered;
        }
        return any;
    }
    bool full() const {
        if (passengers.size() < capacity()) return false;
        for (const CargoType c : {CargoType::Grain, CargoType::Food, CargoType::Goods, CargoType::Logs})
            if (cargo::classQuantity(cargo, cargoClass(c)) < cargoCapacity(c)) return false;
        return true;
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
        if (ar.version == 0 || ar.version >= 14) ar(freeTrack, runs, runsAhead);
        if (ar.version == 0 || ar.version >= 17) ar(cargo, cargoExchangeTick, cargoExchangeUnits, loadingStartTick, cargoLoaded, cargoUnloaded);
        if (ar.version == 0 || ar.version >= 24) ar(passengerSeats);
        if (ar.version == 0 || ar.version >= 29) ar(departures, holdingSince, heldTicks, cycleTicks, satisfactionSum, satisfactionCount);
        if (ar.version == 0 || ar.version >= 32) ar(speedSub);
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
// Trains run at this share of the speed their models are rated for; windows show the rated scale.
inline constexpr i32 kRailSpeedPercent = 50;
i32 maxSpeed(const Vehicle& v);      // units per tick
// A train's weight in quarter tonnes: the cars themselves, and the passengers and freight aboard.
struct TrainWeight {
    i32 empty = 0;
    i32 load = 0;
};
TrainWeight weight(const Vehicle& v);
// The pull of a train's engines at low speed: quarter tonnes accelerated by 1/256 unit per tick
// each tick.
i32 tractiveForce(const Vehicle& v);
// Speeds a train up towards `desired` (units per tick) as hard as its engines can pull its weight
// up the grade under it (fine height units per tile, negative downhill), or brakes down to it.
// Heavy trains take longer to get going and slow down on climbs, though never below a crawl.
void accelerate(Vehicle& v, i32 desired, i32 grade);
// Limit for curves under the consist and braking for curves on reserved track ahead.
i32 trackSpeedLimit(const Vehicle& v);

} // namespace trains

namespace tracktrains {

// Reserved arrival platform, or the platform predicted by the same router as the train.
TrackEdgeId passengerPlatform(const World& w, VehicleId id, const Order& order);
void tick(World& w, VehicleId id);
// Tries to leave the depot; returns true once the train is out on the track.
bool leaveDepot(World& w, VehicleId id);
// Releases every edge the train holds.
void releaseAll(World& w, VehicleId id);

} // namespace tracktrains

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

// The vehicles running one service: the same owner and type with identical orders (vehicles
// sharing an order list always have identical orders). At a timing point they leave `interval`
// ticks apart: the order's interval, or the measured round trip shared out between them (0 while
// unknown).
struct ServiceSpacing {
    i32 vehicles = 0;    // operating, including the one asked about
    i64 roundTrip = 0;   // average measured round trip in ticks, 0 while unknown
    i64 interval = 0;    // ticks between departures, 0 while unknown
    i64 lastDeparture = -1;
};
ServiceSpacing spacing(const World& w, VehicleId id, usize orderIndex);
// The first tick the vehicle may leave the stop of its current order under its timetable.
i64 timetableRelease(const World& w, VehicleId id);

// Vehicle replacement: what a vehicle becomes at its next depot visit under its company's
// autoreplace rules and autorenew (nullopt: it stays as it is).
struct Replacement {
    u16 model = 0;
    std::vector<u16> cars; // trains
    Money price = 0;       // of the new vehicle, inflated
};
std::optional<Replacement> plannedReplacement(const World& w, const Vehicle& v);
// Replaces the vehicle in its depot if a replacement is planned and the company can pay for it.
void autoreplace(World& w, VehicleId id);

} // namespace vehicles

} // namespace tp
