#pragma once
// Commands are the only way to change the game state from outside the simulation. Clients send
// command requests to the server, which stamps them with the sender's company and schedules
// them for a tick; every peer then executes them identically.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/company.hpp"
#include "sim/buildings.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"
#include "sim/track.hpp"
#include "sim/vehicle.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tp {

class World;

// --- Command payloads ----------------------------------------------------------------------

// Explore: make chunks part of the simulated world (spawns their towns).
struct CmdActivateChunks {
    std::vector<ChunkPos> chunks;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(chunks);
    }
};

// Found a new company. colour 0xFF picks the first unused colour.
struct CmdCreateCompany {
    std::string name;
    u8 colour = 0xFF;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(name, colour);
    }
};

// Borrow (positive) or repay (negative) in steps of prices::kLoanStep.
struct CmdSetLoan {
    Money delta = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(delta);
    }
};

// Raise (+1) or lower (-1) a terrain corner.
struct CmdTerraform {
    TilePos corner;
    i8 delta = 1;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(corner, delta);
    }
};

// Smooth terrain brush around a corner (see BrushMode in terraform.hpp).
struct CmdTerraformBrush {
    TilePos corner;
    u8 radius = 3;
    u8 mode = 0;     // BrushMode
    u16 target = 0;  // fine height for BrushMode::Level
    template <class Ar>
    void serialize(Ar& ar) {
        ar(corner, radius, mode, target);
    }
};

// Free-form track between two anchors (see track.hpp). The terrain is shaped to the track.
struct CmdBuildTrack {
    TrackAnchor from;
    TrackAnchor to;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, to);
    }
};

// A straight, level station platform on free-form track, from an anchor towards a point.
// A station of `platforms` parallel platforms: the first from `from` towards `to`, the others
// beside it to its right. Joining an existing station keeps that station's use.
struct CmdBuildTrackStation {
    TrackAnchor from;
    TPoint to;
    StationId join;
    StationUse use = StationUse::Both;
    u8 platforms = 1;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, to, join, use, platforms);
    }
};

// A train depot at the open end of a free-form track.
struct CmdBuildTrackDepot {
    TrackNodeId node;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(node);
    }
};

// Sets the path signals at a node, or on track part way along `edge` (at the point nearest `at`,
// where a node is added). `signals` holds bit s for trains leaving through side s of the node
// (both bits: two-way, none: removed); -1 cycles none, both ways, one way, the other way.
struct CmdTrackSignal {
    TrackNodeId node;
    TrackEdgeId edge;
    TPoint at;
    i32 signals = -1;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(node, edge, at, signals);
    }
};

struct CmdRemoveTrack {
    TrackEdgeId edge;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(edge);
    }
};

// Puts up (or takes down) the overhead wires over a track edge, or over all the company's track
// connected to it.
struct CmdElectrifyTrack {
    TrackEdgeId edge;
    bool connected = false;
    bool remove = false;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(edge, connected, remove);
    }
};

// Bulldoze a rectangular area (inclusive corners).
struct CmdClearArea {
    TilePos a;
    TilePos b;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b);
    }
};

struct CmdPlantTrees {
    TilePos a;
    TilePos b;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b);
    }
};

// Build road (or tram track) along a straight line between two tiles.
struct CmdBuildRoad {
    TilePos from;
    TilePos to;
    bool tram = false;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, to, tram);
    }
};

struct CmdRemoveRoad {
    TilePos from;
    TilePos to;
    bool tram = false;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, to, tram);
    }
};

// A curved road (see road_curves.hpp) from the edge of tile `from` it leaves through `fromSide`
// to the edge of tile `to` it leaves through `toSide`. Either end may be open land or a road.
struct CmdBuildRoadCurve {
    TilePos from;
    Dir fromSide = Dir::N;
    TilePos to;
    Dir toSide = Dir::N;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, fromSide, to, toSide);
    }
};

// Drive-through bus or tram stop on a straight road tile.
struct CmdBuildRoadStop {
    TilePos tile;
    bool tram = false;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tile, tram);
    }
};

// Road / tram depot whose exit faces an adjacent road.
struct CmdBuildRoadDepot {
    TilePos tile;
    Dir facing = Dir::S;
    bool tram = false;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tile, facing, tram);
    }
};

struct CmdBuyVehicle {
    DepotId depot;
    u16 model = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(depot, model);
    }
};

struct CmdSellVehicle {
    VehicleId vehicle;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle);
    }
};

struct CmdStartStopVehicle {
    VehicleId vehicle;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle);
    }
};

struct CmdSendToDepot {
    VehicleId vehicle;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle);
    }
};

// Replaces the vehicle's whole order list.
struct CmdSetOrders {
    VehicleId vehicle;
    std::vector<Order> orders;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle, orders);
    }
};

struct CmdSkipOrder {
    VehicleId vehicle;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle);
    }
};

// Railway track along a dragged line (straight, then diagonal).
struct CmdBuildRail {
    TilePos from;
    TilePos to;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, to);
    }
};

struct CmdRemoveRail {
    TilePos from;
    TilePos to;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, to);
    }
};

// Cycles the signal on a straight track tile: none -> two way -> one way -> other way -> none.
struct CmdCycleSignal {
    TilePos tile;
    bool block = false;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tile, block);
    }
};

// A straight row of platform tiles.
struct CmdBuildRailStation {
    TilePos from;
    TilePos to;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, to);
    }
};

struct CmdBuildRailDepot {
    TilePos tile;
    Dir facing = Dir::S;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tile, facing);
    }
};

struct CmdAddCarriage {
    VehicleId vehicle;
    u16 model = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle, model);
    }
};

struct CmdRemoveCarriage {
    VehicleId vehicle;
    u16 index = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle, index);
    }
};

struct CmdBuildDock {
    TilePos tile;
    Dir facing = Dir::S; // towards the water
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tile, facing);
    }
};

struct CmdBuildShipDepot {
    TilePos tile;
    Dir facing = Dir::S; // towards the water
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tile, facing);
    }
};

// Buys a copy of a vehicle (whole consist for trains) in a depot, copying or sharing its orders.
struct CmdCloneVehicle {
    DepotId depot;
    VehicleId source;
    bool shareOrders = false;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(depot, source, shareOrders);
    }
};

// Gives a vehicle its own copy of a shared order list.
struct CmdUnshareOrders {
    VehicleId vehicle;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(vehicle);
    }
};

// Flood clear, flat sea-level land, or reclaim existing water, in a rectangle.
struct CmdEditWater {
    TilePos a;
    TilePos b;
    bool water = true;
    template <class Ar>
    void serialize(Ar& ar) { ar(a, b, water); }
};

struct CmdBuildSpan {
    TilePos from;
    TilePos to;
    bool rail = false;
    bool tunnel = false;
    template <class Ar> void serialize(Ar& ar) { ar(from, to, rail, tunnel); }
};

struct CmdRemoveSpan {
    TilePos tile;
    template <class Ar> void serialize(Ar& ar) { ar(tile); }
};

struct CmdUpdateCompany {
    std::string name;
    u8 colour = 0;
    template <class Ar> void serialize(Ar& ar) { ar(name, colour); }
};

// A permutation of current car indices, so repeated wagon models remain distinct.
struct CmdReorderConsist {
    VehicleId vehicle;
    std::vector<u16> order;
    template <class Ar> void serialize(Ar& ar) { ar(vehicle, order); }
};

struct CmdStationEntrance {
    StationId station; TilePos road; bool remove = false;
    template<class Ar> void serialize(Ar& ar) { ar(station,road,remove); }
};
struct CmdAddPlatform {
    StationId station; TrackEdgeId platform; i32 side = 1; i32 length = 6;
    template<class Ar> void serialize(Ar& ar) { ar(station,platform,side,length); }
};
struct CmdExtendPlatform {
    StationId station; TrackNodeId end; i32 length = 4;
    template<class Ar> void serialize(Ar& ar) { ar(station,end,length); }
};

struct CmdDemolishBuilding {
    BuildingRef building;
    template<class Ar> void serialize(Ar& ar) { ar(building); }
};

// Autoreplace: replace one vehicle model with another at depot visits (kNoReplacement clears it).
inline constexpr u16 kNoReplacement = 0xFFFF;
struct CmdSetAutoreplace {
    u16 from = 0;
    u16 to = kNoReplacement;
    template <class Ar> void serialize(Ar& ar) { ar(from, to); }
};

// Autorenew: replace vehicles past their life span with new ones at depot visits.
struct CmdSetAutorenew {
    bool enabled = false;
    template <class Ar> void serialize(Ar& ar) { ar(enabled); }
};

// A new line of the given orders, run by `vehicles` (which may be none yet).
struct CmdCreateLine {
    VehicleType type = VehicleType::Rail;
    std::string name;
    std::vector<Order> orders;
    std::vector<VehicleId> vehicles;
    template <class Ar> void serialize(Ar& ar) { ar(type, name, orders, vehicles); }
};
// Renames a line and gives it (and every vehicle running it) new orders, or with `remove` deletes
// the line (its vehicles keep their orders).
struct CmdEditLine {
    u32 line = 0;
    std::string name;
    std::vector<Order> orders;
    bool remove = false;
    template <class Ar> void serialize(Ar& ar) { ar(line, name, orders, remove); }
};
// Puts a vehicle on a line: it takes the line's orders and shares them from then on.
struct CmdAssignLine {
    VehicleId vehicle;
    u32 line = 0;
    template <class Ar> void serialize(Ar& ar) { ar(vehicle, line); }
};

// Turns a rail station over to passengers, freight or both.
struct CmdSetStationUse {
    StationId station;
    StationUse use = StationUse::Both;
    template <class Ar> void serialize(Ar& ar) { ar(station, use); }
};

using CommandPayload =
    std::variant<CmdActivateChunks, CmdCreateCompany, CmdSetLoan, CmdTerraform, CmdClearArea, CmdPlantTrees, CmdBuildRoad,
                 CmdRemoveRoad, CmdBuildRoadStop, CmdBuildRoadDepot, CmdBuyVehicle, CmdSellVehicle, CmdStartStopVehicle,
                 CmdSendToDepot, CmdSetOrders, CmdSkipOrder, CmdBuildRail, CmdRemoveRail, CmdCycleSignal,
                 CmdBuildRailStation, CmdBuildRailDepot, CmdAddCarriage, CmdRemoveCarriage, CmdBuildDock,
                 CmdBuildShipDepot, CmdCloneVehicle, CmdUnshareOrders, CmdEditWater, CmdBuildSpan, CmdRemoveSpan,
                 CmdUpdateCompany, CmdTerraformBrush, CmdBuildTrack, CmdRemoveTrack, CmdBuildTrackStation,
                 CmdBuildTrackDepot, CmdTrackSignal, CmdReorderConsist, CmdStationEntrance, CmdAddPlatform, CmdExtendPlatform,
                 CmdDemolishBuilding, CmdElectrifyTrack, CmdSetAutoreplace, CmdSetAutorenew, CmdSetStationUse,
                 CmdCreateLine, CmdEditLine, CmdAssignLine, CmdBuildRoadCurve>;

struct Command {
    CompanyId company;  // executing company, stamped by the server (invalid = server / spectator)
    u16 client = 0;     // issuing client id (0 = server itself)
    u32 seq = 0;        // client side sequence number, echoed in results
    CommandPayload payload;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(company, client, seq, payload);
    }
};

struct CommandResult {
    bool ok = false;
    Money cost = 0;                          // positive = expense, negative = income
    Expense category = Expense::Construction; // ledger category the cost is booked to
    std::string error;
    u32 createdId = 0xFFFFFFFFu; // id of an object created by the command, if any

    static CommandResult success(Money cost = 0, Expense category = Expense::Construction) {
        CommandResult r;
        r.ok = true;
        r.cost = cost;
        r.category = category;
        return r;
    }
    static CommandResult failure(std::string message) {
        CommandResult r;
        r.error = std::move(message);
        return r;
    }
};

// Validates the command without side effects and returns its cost.
CommandResult testCommand(World& world, const Command& cmd);
// Validates, checks the company can pay, applies the command and books its cost.
CommandResult executeCommand(World& world, const Command& cmd);

std::string_view commandName(const CommandPayload& payload);

} // namespace tp
