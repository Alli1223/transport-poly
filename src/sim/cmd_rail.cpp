// Handlers for railway construction and train consists.

#include "sim/command_handlers.hpp"
#include "sim/passengers.hpp"
#include "sim/prices.hpp"
#include "sim/rail.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <bit>
#include <format>

namespace tp::cmd {

namespace {

std::string whyNoRail(const World& w, TilePos t) {
    const Tile* tile = w.map.tile(t);
    if (!tile) return "Area not explored";
    switch (tile->type) {
    case TileType::Water: return "Can't build on water";
    case TileType::Building: return "Building in the way";
    case TileType::Road: return "Road in the way";
    case TileType::Station: return "Station in the way";
    case TileType::Depot: return "Depot in the way";
    default: break;
    }
    if (w.map.slope(t).isSteep()) return "Land sloped in the wrong direction";
    if (tile->type == TileType::Rail && tile->extra) return "Remove the signal first";
    return "Can't build railway here";
}

Vehicle* ownedTrainInDepot(World& w, const Command& c, VehicleId id, std::string& error) {
    Vehicle* v = w.vehicles.tryGet(id);
    if (!v || v->owner != c.company || v->info().type != VehicleType::Rail) {
        error = "No such train";
        return nullptr;
    }
    if (v->state != VehicleState::InDepot || !v->stopped) {
        error = "The train must be stopped inside a depot";
        return nullptr;
    }
    return v;
}

} // namespace

CommandResult handle(World& w, const Command& c, const CmdBuildRail& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build railways");
    if (x.from == x.to) return CommandResult::failure("Drag to build track");
    const std::vector<RailStep> steps = planRailLine(x.from, x.to);
    if (steps.empty()) return CommandResult::failure("Drag to build track");
    if (steps.size() > 400) return CommandResult::failure("Track too long");
    Money cost = 0;
    int built = 0;
    for (const RailStep& s : steps) {
        Tile* t = w.map.tile(s.tile);
        if (!t) return CommandResult::failure("Area not explored");
        if (t->type == TileType::Rail && (t->rail & s.bit)) continue; // already there
        if (t->type == TileType::Rail && t->owner != c.company.value)
            return CommandResult::failure("Owned by another company");
        if (t->type == TileType::Road && t->rail) {
            if (t->owner != c.company.value) return CommandResult::failure("Owned by another company");
            if (t->rail & s.bit) continue;
        }
        if (!canAddTrack(w.map, s.tile, s.bit)) return CommandResult::failure(whyNoRail(w, s.tile));
        if (t->type == TileType::Road && crossingRoadOccupied(w, s.tile))
            return CommandResult::failure("Vehicle in the way");
        cost += prices::kRailPiece + (t->type == TileType::Clear ? t->trees * prices::kRemoveTree : 0);
        ++built;
        if (exec) {
            if (t->type == TileType::Clear) {
                t->type = TileType::Rail;
                t->rail = 0;
                t->extra = 0;
                t->extra2 = 0;
                t->trees = 0;
                t->owner = c.company.value;
                if (t->ground == Ground::Rock || t->ground == Ground::Rough) t->ground = Ground::Grass;
            }
            t->rail = static_cast<u8>(t->rail | s.bit);
            t->owner = c.company.value;
            if (t->type == TileType::Road) w.signalTiles.insert(ChunkPos{s.tile.x, s.tile.y}.key());
            w.map.touch(s.tile);
        }
    }
    if (built == 0) return CommandResult::failure("Already built");
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdRemoveRail& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can remove railways");
    std::vector<RailStep> steps = planRailLine(x.from, x.to);
    if (x.from == x.to) {
        // Single tile: remove all of its track.
        const Tile* t = w.map.tile(x.from);
        if (t && (t->type == TileType::Rail || t->type == TileType::Road)) steps.push_back({x.from, t->rail});
    }
    Money cost = 0;
    int removed = 0;
    for (const RailStep& s : steps) {
        Tile* t = w.map.tile(s.tile);
        if (!t) return CommandResult::failure("Area not explored");
        if ((t->type != TileType::Rail && t->type != TileType::Road) || !(t->rail & s.bit)) continue;
        if (t->owner != c.company.value) return CommandResult::failure("Owned by another company");
        if (t->extra2 & s.bit) return CommandResult::failure("Train in the way");
        const u8 bits = t->rail & s.bit;
        cost += std::popcount(static_cast<unsigned>(bits)) * prices::kRemoveRailPiece;
        ++removed;
        if (exec) {
            t->rail = static_cast<u8>(t->rail & ~bits);
            if (!isStraightTrack(t->rail)) t->extra = 0; // signals need a single straight track
            if (t->rail == 0) {
                w.signalTiles.erase(ChunkPos{s.tile.x, s.tile.y}.key());
                t->type = t->road ? TileType::Road : TileType::Clear;
                t->owner = kOwnerNone;
                t->extra = 0;
                t->extra2 = 0;
            }
            w.map.touch(s.tile);
        }
    }
    if (removed == 0) return CommandResult::failure("No track to remove");
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdCycleSignal& x, bool exec) {
    Tile* t = w.map.tile(x.tile);
    if (!t) return CommandResult::failure("Area not explored");
    if (t->type != TileType::Rail) return CommandResult::failure("Signals go on railway track");
    if (t->owner != c.company.value) return CommandResult::failure("Owned by another company");
    if (!isStraightTrack(t->rail)) return CommandResult::failure("Signals need a single straight track");
    if (t->extra2) return CommandResult::failure("Train in the way");
    static constexpr u8 kNext[4] = {3, 2, 0, 1}; // 0 -> 3 (two way) -> 1 -> 2 -> 0
    const u8 directions = t->extra & 3;
    const bool changeKind = directions && ((t->extra & kSignalBlock) != 0) != x.block;
    const u8 nextDirections = changeKind ? directions : kNext[directions];
    const u8 next = nextDirections ? static_cast<u8>(nextDirections | (x.block ? kSignalBlock : 0)) : 0;
    const Money cost = t->extra == 0 ? prices::kSignal : 0;
    if (exec) {
        t->extra = next;
        w.map.touch(x.tile);
        w.signalTiles.insert(ChunkPos{x.tile.x, x.tile.y}.key());
        if (next == 0) w.signalTiles.erase(ChunkPos{x.tile.x, x.tile.y}.key());
    }
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdBuildRailStation& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build stations");
    if (x.from.x != x.to.x && x.from.y != x.to.y) return CommandResult::failure("Platforms must be straight");
    const bool alongX = x.from.y == x.to.y && x.from.x != x.to.x;
    const u8 bit = alongX ? kTrackEW : kTrackNS;
    const Dir step = alongX ? (x.to.x >= x.from.x ? Dir::E : Dir::W) : (x.to.y >= x.from.y ? Dir::S : Dir::N);
    std::vector<TilePos> tiles;
    for (TilePos t = x.from;; t = neighbour(t, step)) {
        tiles.push_back(t);
        if (t == x.to) break;
    }
    if (tiles.size() > 16) return CommandResult::failure("Platform too long (max 16 tiles)");
    for (const TilePos p : tiles)
        if (const std::string why = towns::authorityRefusal(w, c.company, p, kRatingRefuseBuild); !why.empty())
            return CommandResult::failure(why);
    int level = -1;
    Money cost = 0;
    for (const TilePos p : tiles) {
        const Tile* t = w.map.tile(p);
        if (!t) return CommandResult::failure("Area not explored");
        const Slope s = w.map.slope(p);
        if (!s.isFlat()) return CommandResult::failure("Land must be flat");
        if (level >= 0 && s.base != level) return CommandResult::failure("Land must be level");
        level = s.base;
        const bool ownTrack = t->type == TileType::Rail && t->rail == bit && t->owner == c.company.value &&
                              t->extra == 0 && t->extra2 == 0;
        if (t->type != TileType::Clear && !ownTrack) return CommandResult::failure(whyNoRail(w, p));
        cost += prices::kRailStationTile + (t->type == TileType::Clear ? t->trees * prices::kRemoveTree : 0);
    }
    if (exec) {
        StationId sid;
        for (const TilePos p : tiles) {
            if (!sid.valid()) sid = stations::findJoinable(w, c.company, p);
            if (sid.valid()) break;
        }
        for (const TilePos p : tiles) {
            Tile& t = *w.map.tile(p);
            t.type = TileType::Station;
            t.extra = static_cast<u8>(StopKind::Rail);
            t.extra2 = 0;
            t.rail = bit;
            t.road = 0;
            t.trees = 0;
            t.owner = c.company.value;
            if (sid.valid()) {
                t.index = sid.value;
                stations::addTile(w, sid, p, StopKind::Rail);
            } else {
                sid = stations::create(w, c.company, p, StopKind::Rail);
                w.map.tile(p)->index = sid.value;
            }
            w.map.touch(p);
        }
        passengers::markDirty(w);
    }
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdBuildRailDepot& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build depots");
    const Tile* t = w.map.tile(x.tile);
    if (!t) return CommandResult::failure("Area not explored");
    if (t->type != TileType::Clear) return CommandResult::failure("Site must be clear");
    const Slope s = w.map.slope(x.tile);
    if (!s.isFlat()) return CommandResult::failure("Land must be flat");
    if (const std::string why = towns::authorityRefusal(w, c.company, x.tile, kRatingRefuseBuild); !why.empty())
        return CommandResult::failure(why);
    const TilePos front = neighbour(x.tile, x.facing);
    const Tile* ft = w.map.tile(front);
    if (!ft) return CommandResult::failure("Area not explored");
    const Dir back = opposite(x.facing);
    const u8 straight = (x.facing == Dir::N || x.facing == Dir::S) ? kTrackNS : kTrackEW;
    const bool connected = ft->type == TileType::Rail && (ft->rail & tracksAtEdge(back));
    bool addPiece = false;
    if (!connected) {
        if (!canAddTrack(w.map, front, straight)) return CommandResult::failure("The depot must face railway track");
        if (ft->type == TileType::Rail && ft->owner != c.company.value)
            return CommandResult::failure("Owned by another company");
        addPiece = true;
    }
    const Money cost = prices::kRailDepot + (addPiece ? prices::kRailPiece : 0) + t->trees * prices::kRemoveTree;
    if (!exec) return CommandResult::success(cost, Expense::Construction);
    if (addPiece) {
        Tile& f = *w.map.tile(front);
        if (f.type == TileType::Clear) {
            f.type = TileType::Rail;
            f.rail = 0;
            f.extra = 0;
            f.extra2 = 0;
            f.trees = 0;
            f.owner = c.company.value;
        }
        f.rail = static_cast<u8>(f.rail | straight);
        w.map.touch(front);
    }
    const DepotId id = w.depots.create();
    Depot& d = w.depots[id];
    d.owner = c.company;
    d.tile = x.tile;
    d.facing = x.facing;
    d.kind = DepotKind::Rail;
    d.town = stations::nearestTown(w, x.tile);
    Tile& dt = *w.map.tile(x.tile);
    dt.type = TileType::Depot;
    dt.extra = static_cast<u8>(x.facing);
    dt.extra2 = 0;
    dt.index = id.value;
    dt.owner = c.company.value;
    dt.trees = 0;
    dt.road = 0;
    dt.rail = straight;
    w.map.touch(x.tile);
    CommandResult r = CommandResult::success(cost, Expense::Construction);
    r.createdId = id.value;
    return r;
}

CommandResult handle(World& w, const Command& c, const CmdAddCarriage& x, bool exec) {
    std::string error;
    Vehicle* v = ownedTrainInDepot(w, c, x.vehicle, error);
    if (!v) return CommandResult::failure(error);
    if (x.model >= vehicleModels().size()) return CommandResult::failure("Unknown vehicle");
    const VehicleModel& m = vehicleModel(x.model);
    if (m.type != VehicleType::Rail) return CommandResult::failure("Not a railway vehicle");
    if (m.introYear > w.clock.date().year) return CommandResult::failure("Not yet available");
    if (v->cars.size() >= 12) return CommandResult::failure("Train too long");
    if (exec) {
        v->cars.push_back(x.model);
        v->value += m.price;
        passengers::markDirty(w);
    }
    return CommandResult::success(m.price, Expense::NewVehicles);
}

CommandResult handle(World& w, const Command& c, const CmdRemoveCarriage& x, bool exec) {
    std::string error;
    Vehicle* v = ownedTrainInDepot(w, c, x.vehicle, error);
    if (!v) return CommandResult::failure(error);
    if (x.index == 0 || x.index >= v->cars.size())
        return CommandResult::failure("Sell the whole train to remove its engine");
    const Money refund = vehicleModel(v->cars[x.index]).price * 3 / 4;
    if (exec) {
        v->value = std::max<Money>(0, v->value - refund);
        v->cars.erase(v->cars.begin() + x.index);
        passengers::markDirty(w);
    }
    return CommandResult::success(-refund, Expense::NewVehicles);
}

} // namespace tp::cmd
