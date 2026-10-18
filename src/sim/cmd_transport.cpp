// Handlers for roads, stops, depots, demolition and vehicle commands.

#include "sim/command_handlers.hpp"
#include "sim/passengers.hpp"
#include "sim/prices.hpp"
#include "sim/road.hpp"
#include "sim/road_path.hpp"
#include "sim/rail.hpp"
#include "sim/water_path.hpp"
#include "sim/world.hpp"

#include <bit>
#include <format>
#include <unordered_set>

namespace tp::cmd {

namespace {

bool lineTiles(TilePos a, TilePos b, std::vector<TilePos>& out, Dir& dir) {
    out.clear();
    if (a.x != b.x && a.y != b.y) return false;
    if (a.x == b.x) dir = b.y >= a.y ? Dir::S : Dir::N;
    else dir = b.x >= a.x ? Dir::E : Dir::W;
    TilePos t = a;
    out.push_back(t);
    while (t != b) {
        t = neighbour(t, dir);
        out.push_back(t);
    }
    return true;
}

bool occupied(const std::unordered_set<u64>& set, TilePos t) { return set.contains(ChunkPos{t.x, t.y}.key()); }

std::string whyNoRoad(const World& w, TilePos t) {
    const Tile* tile = w.map.tile(t);
    if (!tile) return "Area not explored";
    switch (tile->type) {
    case TileType::Water: return "Can't build on water";
    case TileType::Building: return "Building in the way";
    case TileType::Station: return "Station in the way";
    case TileType::Depot: return "Depot in the way";
    case TileType::Rail: return "Railway in the way";
    default: break;
    }
    if (w.map.slope(t).isSteep()) return "Land sloped in the wrong direction";
    return "Can't build road here";
}

u8 ownerByte(CompanyId c) { return c.value; }

CommandResult demolishImpl(World& w, CompanyId company, TilePos t, bool exec, const std::unordered_set<u64>& busy) {
    Tile* tile = w.map.tile(t);
    if (!tile) return CommandResult::failure("Area not explored");
    if (tile->span) return CommandResult::failure("Use the remove bridge / tunnel tool");
    switch (tile->type) {
    case TileType::Clear: {
        Money cost = static_cast<Money>(tile->trees) * prices::kRemoveTree;
        if (tile->ground == Ground::Rough) cost += prices::kClearRough;
        if (tile->ground == Ground::Rock) cost += prices::kClearRock;
        if (cost == 0) return CommandResult::failure("Nothing to clear");
        if (exec) {
            if (tile->trees > 0)
                towns::changeRating(w, towns::townAt(w, t), company, kRatingRemoveTrees, kRatingRefuseBuild);
            tile->trees = 0;
            if (tile->ground == Ground::Rough || tile->ground == Ground::Rock) tile->ground = Ground::Grass;
            w.map.touch(t);
        }
        return CommandResult::success(cost);
    }
    case TileType::Water: return CommandResult::failure("Can't clear water");
    case TileType::Road: {
        if (tile->rail && tile->owner != ownerByte(company))
            return CommandResult::failure("Railway owned by another company");
        if (tile->rail && tile->extra2) return CommandResult::failure("Train in the way");
        if (roadPieces(*tile) && tile->roadOwner == kOwnerTown) return CommandResult::failure("Town road - can't remove");
        if (roadPieces(*tile) && tile->roadOwner != ownerByte(company) && tile->roadOwner != kOwnerNone)
            return CommandResult::failure("Owned by another company");
        if (tramPieces(*tile) && tile->owner != ownerByte(company) && tile->owner != kOwnerNone)
            return CommandResult::failure("Tram track owned by another company");
        if (occupied(busy, t)) return CommandResult::failure("Vehicle in the way");
        const Money cost = std::popcount(static_cast<unsigned>(tile->road)) * prices::kRemoveRoadPiece +
                           std::popcount(static_cast<unsigned>(tile->rail)) * prices::kRemoveRailPiece;
        if (exec) {
            if (tile->rail) w.signalTiles.erase(ChunkPos{t.x, t.y}.key());
            tile->type = TileType::Clear;
            tile->road = 0;
            tile->rail = 0;
            tile->extra2 = 0;
            tile->owner = kOwnerNone;
            tile->roadOwner = kOwnerNone;
            w.map.touch(t);
            vehicles::invalidatePaths(w);
        }
        return CommandResult::success(cost);
    }
    case TileType::Station: {
        const StationId sid(tile->index);
        const Station* s = w.stations.tryGet(sid);
        if (s && s->owner != company) return CommandResult::failure("Owned by another company");
        if (static_cast<StopKind>(tile->extra) == StopKind::Rail && tile->extra2)
            return CommandResult::failure("Train in the way");
        if (occupied(busy, t)) return CommandResult::failure("Vehicle in the way");
        if (exec) {
            const StopKind kind = static_cast<StopKind>(tile->extra);
            if (kind == StopKind::Bus || kind == StopKind::Tram) {
                tile->type = TileType::Road;
            } else {
                tile->type = TileType::Clear;
                tile->road = 0;
                tile->rail = 0;
            }
            tile->extra = 0;
            tile->extra2 = 0;
            tile->index = 0;
            if (tramPieces(*tile) == 0) tile->owner = kOwnerNone;
            w.map.touch(t);
            if (s) stations::removeTile(w, sid, t);
            passengers::markDirty(w);
            vehicles::invalidatePaths(w);
        }
        return CommandResult::success(prices::kRemoveStation);
    }
    case TileType::Depot: {
        const DepotId did(tile->index);
        const Depot* d = w.depots.tryGet(did);
        if (d && d->owner != company) return CommandResult::failure("Owned by another company");
        for (const auto [vid, v] : w.vehicles)
            if (v.depot == did && v.state == VehicleState::InDepot)
                return CommandResult::failure("Vehicles are parked in this depot");
        if (occupied(busy, t)) return CommandResult::failure("Vehicle in the way");
        if (exec) {
            tile->type = TileType::Clear;
            tile->road = 0;
            tile->rail = 0;
            tile->extra = 0;
            tile->extra2 = 0;
            tile->index = 0;
            tile->owner = kOwnerNone;
            w.map.touch(t);
            for (auto [vid, v] : w.vehicles)
                if (v.depot == did) v.depot = DepotId{};
            if (d) w.depots.destroy(did);
            vehicles::invalidatePaths(w);
        }
        return CommandResult::success(prices::kRemoveDepot);
    }
    case TileType::Building: {
        const BuildingId bid(tile->index);
        const Building* b = w.buildings.tryGet(bid);
        if (!b) return CommandResult::failure("Nothing to clear");
        // Checked in the test phase only, so that a large clearance is all or nothing.
        if (!exec)
            if (const std::string why = towns::authorityRefusal(w, company, t, kRatingRefuseDemolish); !why.empty())
                return CommandResult::failure(why);
        const Money cost = prices::kDemolishBuilding + b->residents * prices::kDemolishPerResident;
        if (exec) {
            towns::changeRating(w, b->town, company, kRatingDemolishBuilding);
            towns::removeBuilding(w, bid);
        }
        return CommandResult::success(cost);
    }
    case TileType::Rail: {
        if (tile->owner != ownerByte(company) && tile->owner != kOwnerNone)
            return CommandResult::failure("Owned by another company");
        if (tile->extra2) return CommandResult::failure("Train in the way");
        const Money cost = std::popcount(static_cast<unsigned>(tile->rail)) * prices::kRemoveRailPiece +
                           (tile->extra ? prices::kSignal / 5 : 0);
        if (exec) {
            if (tile->extra) w.signalTiles.erase(ChunkPos{t.x, t.y}.key());
            tile->type = TileType::Clear;
            tile->rail = 0;
            tile->extra = 0;
            tile->extra2 = 0;
            tile->owner = kOwnerNone;
            w.map.touch(t);
        }
        return CommandResult::success(cost);
    }
    }
    return CommandResult::failure("Can't clear this");
}

bool stopKindMatches(VehicleType type, StopKind k) {
    switch (type) {
    case VehicleType::Road: return k == StopKind::Bus;
    case VehicleType::Tram: return k == StopKind::Tram;
    case VehicleType::Rail: return k == StopKind::Rail;
    case VehicleType::Ship: return k == StopKind::Dock;
    }
    return false;
}

bool depotKindMatches(VehicleType type, DepotKind k) {
    switch (type) {
    case VehicleType::Road: return k == DepotKind::Road;
    case VehicleType::Tram: return k == DepotKind::Tram;
    case VehicleType::Rail: return k == DepotKind::Rail;
    case VehicleType::Ship: return k == DepotKind::Ship;
    }
    return false;
}

Vehicle* ownedVehicle(World& w, const Command& c, VehicleId id) {
    Vehicle* v = w.vehicles.tryGet(id);
    return (v && v->owner == c.company) ? v : nullptr;
}

} // namespace

std::unordered_set<u64> occupiedTiles(const World& w) {
    std::unordered_set<u64> set;
    for (const auto [id, v] : w.vehicles)
        if (v.state != VehicleState::InDepot) set.insert(ChunkPos{v.tile.x, v.tile.y}.key());
    return set;
}

CommandResult demolishTile(World& w, CompanyId company, TilePos t, bool exec, const std::unordered_set<u64>& busy) {
    return demolishImpl(w, company, t, exec, busy);
}

CommandResult handle(World& w, const Command& c, const CmdBuildRoad& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build roads");
    std::vector<TilePos> tiles;
    Dir dir;
    if (x.from == x.to) return CommandResult::failure("Drag to build a road");
    if (!lineTiles(x.from, x.to, tiles, dir)) return CommandResult::failure("Roads must be built in a straight line");
    if (tiles.size() > 256) return CommandResult::failure("Road too long");
    Money cost = 0;
    int built = 0;
    for (usize i = 0; i + 1 < tiles.size(); ++i) {
        const TilePos a = tiles[i];
        const TilePos b = tiles[i + 1];
        const Tile* ta = w.map.tile(a);
        const Tile* tb = w.map.tile(b);
        if (!ta || !tb) return CommandResult::failure("Area not explored");
        const bool hasA = layerPieces(*ta, x.tram) & dirBit(dir);
        const bool hasB = layerPieces(*tb, x.tram) & dirBit(opposite(dir));
        if (hasA && hasB) continue;
        if (!canConnectRoad(w.map, a, dir, x.tram)) {
            const bool aOk = ta->type == TileType::Clear || ta->type == TileType::Road;
            return CommandResult::failure(whyNoRoad(w, aOk ? b : a));
        }
        cost += (hasA ? 0 : prices::kRoadPiece) + (hasB ? 0 : prices::kRoadPiece);
        if (ta->type == TileType::Clear) cost += ta->trees * prices::kRemoveTree;
        if (tb->type == TileType::Clear && i + 2 == tiles.size()) cost += tb->trees * prices::kRemoveTree;
        ++built;
        if (exec) {
            connectRoad(w.map, a, dir, ownerByte(c.company), x.tram);
            for (const TilePos pos : {a, b})
                if (w.map.tile(pos)->rail) w.signalTiles.insert(ChunkPos{pos.x, pos.y}.key());
        }
    }
    if (built == 0) return CommandResult::failure("Already built");
    if (exec) vehicles::invalidatePaths(w);
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdRemoveRoad& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can remove roads");
    std::vector<TilePos> tiles;
    Dir dir;
    if (!lineTiles(x.from, x.to, tiles, dir)) return CommandResult::failure("Select a straight line");
    const std::unordered_set<u64> busy = occupiedTiles(w);
    Money cost = 0;
    int removed = 0;
    for (usize i = 0; i < tiles.size(); ++i) {
        Tile* t = w.map.tile(tiles[i]);
        if (!t) return CommandResult::failure("Area not explored");
        if (t->type != TileType::Road) continue;
        if (t->span) return CommandResult::failure("Use the remove bridge / tunnel tool");
        u8 mask = 0;
        if (i + 1 < tiles.size()) mask |= dirBit(dir);
        if (i > 0) mask |= dirBit(opposite(dir));
        if (tiles.size() == 1) mask = 0x0F;
        const u8 have = layerPieces(*t, x.tram) & mask;
        if (!have) continue;
        const u8 owner = x.tram ? t->owner : t->roadOwner;
        if (owner == kOwnerTown) return CommandResult::failure("Town road - can't remove");
        if (owner != ownerByte(c.company) && owner != kOwnerNone) return CommandResult::failure("Owned by another company");
        if (occupied(busy, tiles[i])) return CommandResult::failure("Vehicle in the way");
        cost += std::popcount(static_cast<unsigned>(have)) * prices::kRemoveRoadPiece;
        ++removed;
        if (exec) {
            t->road = static_cast<u8>(t->road & ~(x.tram ? (have << 4) : have));
            if (t->road == 0) {
                if (t->rail) w.signalTiles.erase(ChunkPos{tiles[i].x, tiles[i].y}.key());
                t->type = t->rail ? TileType::Rail : TileType::Clear;
                if (!t->rail) t->owner = kOwnerNone;
                t->roadOwner = kOwnerNone;
            }
            w.map.touch(tiles[i]);
        }
    }
    if (removed == 0) return CommandResult::failure("No road to remove");
    if (exec) vehicles::invalidatePaths(w);
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdBuildRoadStop& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build stops");
    Tile* t = w.map.tile(x.tile);
    if (!t) return CommandResult::failure("Area not explored");
    if (t->type != TileType::Road) return CommandResult::failure(x.tram ? "Build on a tram track" : "Build on a road");
    if (t->span) return CommandResult::failure("Stops cannot be built on a bridge or tunnel");
    if (t->rail) return CommandResult::failure("Can't build a stop on a level crossing");
    const u8 pieces = layerPieces(*t, x.tram);
    if (!isStraight(pieces)) return CommandResult::failure("The road must be straight here");
    if (!w.map.slope(x.tile).isFlat()) return CommandResult::failure("Land must be flat");
    if (x.tram && t->owner != c.company.value) return CommandResult::failure("Tram track owned by someone else");
    if (const std::string why = towns::authorityRefusal(w, c.company, x.tile, kRatingRefuseBuild); !why.empty())
        return CommandResult::failure(why);
    if (exec) {
        const StopKind kind = x.tram ? StopKind::Tram : StopKind::Bus;
        StationId sid = stations::findJoinable(w, c.company, x.tile);
        t->type = TileType::Station;
        t->extra = static_cast<u8>(kind);
        if (sid.valid()) {
            t->index = sid.value;
            stations::addTile(w, sid, x.tile, kind);
        } else {
            sid = stations::create(w, c.company, x.tile, kind);
            w.map.tile(x.tile)->index = sid.value;
        }
        w.map.touch(x.tile);
        passengers::markDirty(w);
        vehicles::invalidatePaths(w);
    }
    return CommandResult::success(prices::kRoadStop, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdBuildRoadDepot& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build depots");
    const Tile* t = w.map.tile(x.tile);
    if (!t) return CommandResult::failure("Area not explored");
    if (t->type != TileType::Clear) return CommandResult::failure("Site must be clear");
    const Slope s = w.map.slope(x.tile);
    if (!s.isFlat()) return CommandResult::failure("Land must be flat");
    const TilePos front = neighbour(x.tile, x.facing);
    const Tile* ft = w.map.tile(front);
    if (!ft || ft->type != TileType::Road) return CommandResult::failure("The depot must face a road");
    // The road in front needs a piece towards the depot at a matching height.
    const u8 bit = x.tram ? static_cast<u8>(dirBit(opposite(x.facing)) << 4) : dirBit(opposite(x.facing));
    const u8 newRoad = static_cast<u8>(ft->road | bit);
    const Surface fs = roadSurface(w.map.slope(front), static_cast<u8>((newRoad & 0x0F) | (newRoad >> 4)));
    if (fs == Surface::Invalid) return CommandResult::failure("Can't connect to the road");
    if (roadEdgeHalfLevels(w.map.corners(front), fs, opposite(x.facing)) != 2 * s.base)
        return CommandResult::failure("The road is at a different height");
    if (const std::string why = towns::authorityRefusal(w, c.company, x.tile, kRatingRefuseBuild); !why.empty())
        return CommandResult::failure(why);
    const bool needPiece = !(layerPieces(*ft, x.tram) & dirBit(opposite(x.facing)));
    const Money cost = prices::kRoadDepot + (needPiece ? prices::kRoadPiece : 0) + t->trees * prices::kRemoveTree;
    if (exec) {
        if (needPiece && !addRoadPiece(w.map, front, opposite(x.facing), c.company.value, x.tram))
            return CommandResult::failure("Can't connect to the road");
        const DepotId id = w.depots.create();
        Depot& d = w.depots[id];
        d.owner = c.company;
        d.tile = x.tile;
        d.facing = x.facing;
        d.kind = x.tram ? DepotKind::Tram : DepotKind::Road;
        d.town = stations::nearestTown(w, x.tile);
        Tile& dt = *w.map.tile(x.tile);
        dt.type = TileType::Depot;
        dt.extra = static_cast<u8>(x.facing);
        dt.index = id.value;
        dt.owner = c.company.value;
        dt.trees = 0;
        dt.road = x.tram ? static_cast<u8>(dirBit(x.facing) << 4) : dirBit(x.facing);
        w.map.touch(x.tile);
        vehicles::invalidatePaths(w);
        CommandResult r = CommandResult::success(cost, Expense::Construction);
        r.createdId = id.value;
        return r;
    }
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdBuyVehicle& x, bool exec) {
    const Depot* d = w.depots.tryGet(x.depot);
    if (!d || d->owner != c.company) return CommandResult::failure("No such depot");
    if (x.model >= vehicleModels().size()) return CommandResult::failure("Unknown vehicle");
    const VehicleModel& m = vehicleModel(x.model);
    if (!depotKindMatches(m.type, d->kind)) return CommandResult::failure("This depot can't build that vehicle");
    if (m.type == VehicleType::Rail && !m.powered)
        return CommandResult::failure("Buy an engine first, then add carriages to it");
    if (m.introYear > w.clock.date().year) return CommandResult::failure("Not yet available");
    if (w.vehicles.size() >= 60'000) return CommandResult::failure("Too many vehicles");
    if (exec) {
        const VehicleId id = vehicles::buy(w, c.company, x.depot, x.model);
        CommandResult r = CommandResult::success(m.price, Expense::NewVehicles);
        r.createdId = id.value;
        return r;
    }
    return CommandResult::success(m.price, Expense::NewVehicles);
}

CommandResult handle(World& w, const Command& c, const CmdSellVehicle& x, bool exec) {
    Vehicle* v = ownedVehicle(w, c, x.vehicle);
    if (!v) return CommandResult::failure("No such vehicle");
    if (v->state != VehicleState::InDepot || !v->stopped)
        return CommandResult::failure("Vehicle must be stopped inside a depot");
    const Money value = v->value;
    if (exec) {
        vehicles::sell(w, x.vehicle);
        passengers::markDirty(w);
    }
    return CommandResult::success(-value, Expense::NewVehicles);
}

CommandResult handle(World& w, const Command& c, const CmdStartStopVehicle& x, bool exec) {
    Vehicle* v = ownedVehicle(w, c, x.vehicle);
    if (!v) return CommandResult::failure("No such vehicle");
    if (exec) {
        v->stopped = !v->stopped;
        passengers::markDirty(w);
    }
    return CommandResult::success();
}

CommandResult handle(World& w, const Command& c, const CmdSendToDepot& x, bool exec) {
    Vehicle* v = ownedVehicle(w, c, x.vehicle);
    if (!v) return CommandResult::failure("No such vehicle");
    if (!w.depots.contains(v->depot)) return CommandResult::failure("The vehicle has no depot to return to");
    if (exec) {
        v->sendToDepot = !v->sendToDepot;
        v->path.clear();
        v->pathIndex = 0;
    }
    return CommandResult::success();
}

CommandResult handle(World& w, const Command& c, const CmdSetOrders& x, bool exec) {
    Vehicle* v = ownedVehicle(w, c, x.vehicle);
    if (!v) return CommandResult::failure("No such vehicle");
    if (x.orders.size() > 64) return CommandResult::failure("Too many orders");
    const VehicleType type = v->info().type;
    for (const Order& o : x.orders) {
        if (o.flags & ~(kOrderNonStop | kOrderFullLoad | kOrderUnloadOnly | kOrderNoLoading))
            return CommandResult::failure("Unknown order flags");
        if ((o.flags & kOrderFullLoad) && (o.flags & (kOrderNonStop | kOrderUnloadOnly | kOrderNoLoading)))
            return CommandResult::failure("Full load requires a stopping order that permits loading");
        if (o.kind == OrderKind::Station) {
            const Station* s = w.stations.tryGet(o.station);
            if (!s || s->owner != c.company) return CommandResult::failure("Invalid station in orders");
            bool ok = false;
            for (const TilePos t : s->tiles) {
                const Tile* tile = w.map.tile(t);
                if (tile && tile->type == TileType::Station && stopKindMatches(type, static_cast<StopKind>(tile->extra)))
                    ok = true;
            }
            if (!ok) return CommandResult::failure(std::format("{} has no stop for this vehicle", s->name));
        } else if (o.kind == OrderKind::Via) {
            const Tile* tile = w.map.tile(o.waypoint);
            const bool ok = tile && (type == VehicleType::Ship ? isWaterTile(w, o.waypoint)
                : type == VehicleType::Rail ? tile->rail != 0 && tile->type != TileType::Depot
                : drivablePieces(w, o.waypoint, type, DepotId{}) != 0 && tile->type != TileType::Depot);
            if (!ok) return CommandResult::failure("Waypoint needs navigable infrastructure for this vehicle");
        } else if (o.kind == OrderKind::Depot) {
            const Depot* d = w.depots.tryGet(o.depot);
            if (!d || d->owner != c.company || !depotKindMatches(type, d->kind))
                return CommandResult::failure("Invalid depot in orders");
        } else {
            return CommandResult::failure("Unknown order kind");
        }
    }
    if (exec) {
        const u32 group = v->orderGroup;
        const CompanyId owner = v->owner;
        for (auto [id, o] : w.vehicles) {
            if (id != x.vehicle && (group == 0 || o.orderGroup != group || o.owner != owner)) continue;
            o.orders = x.orders;
            if (o.orderIndex >= o.orders.size()) o.orderIndex = 0;
            o.path.clear();
            o.pathIndex = 0;
        }
        passengers::markDirty(w);
    }
    return CommandResult::success();
}

CommandResult handle(World& w, const Command& c, const CmdCloneVehicle& x, bool exec) {
    const Vehicle* src = ownedVehicle(w, c, x.source);
    if (!src) return CommandResult::failure("No such vehicle");
    const Depot* d = w.depots.tryGet(x.depot);
    if (!d || d->owner != c.company) return CommandResult::failure("No such depot");
    const VehicleModel& m = src->info();
    if (!depotKindMatches(m.type, d->kind)) return CommandResult::failure("This depot can't build that vehicle");
    if (w.vehicles.size() >= 60'000) return CommandResult::failure("Too many vehicles");
    const std::vector<u16> cars = src->cars;
    Money cost = 0;
    if (cars.empty()) {
        cost = m.price;
    } else {
        for (const u16 car : cars) cost += vehicleModel(car).price;
    }
    if (!exec) return CommandResult::success(cost, Expense::NewVehicles);
    const u16 model = src->model;
    const std::vector<Order> orders = src->orders;
    u32 group = src->orderGroup;
    if (x.shareOrders && group == 0) group = src->serial;
    const VehicleId id = vehicles::buy(w, c.company, x.depot, model); // may move `src`
    Vehicle& v = w.vehicles[id];
    if (!cars.empty()) v.cars = cars;
    v.value = w.inflated(cost);
    v.orders = orders;
    if (x.shareOrders) {
        v.orderGroup = group;
        w.vehicles[x.source].orderGroup = group;
    }
    passengers::markDirty(w);
    CommandResult r = CommandResult::success(cost, Expense::NewVehicles);
    r.createdId = id.value;
    return r;
}

CommandResult handle(World& w, const Command& c, const CmdUnshareOrders& x, bool exec) {
    Vehicle* v = ownedVehicle(w, c, x.vehicle);
    if (!v) return CommandResult::failure("No such vehicle");
    if (v->orderGroup == 0) return CommandResult::failure("Orders are not shared");
    if (exec) v->orderGroup = 0;
    return CommandResult::success();
}

CommandResult handle(World& w, const Command& c, const CmdSkipOrder& x, bool exec) {
    Vehicle* v = ownedVehicle(w, c, x.vehicle);
    if (!v) return CommandResult::failure("No such vehicle");
    if (v->orders.empty()) return CommandResult::failure("No orders to skip");
    if (exec) {
        v->orderIndex = static_cast<u8>((v->orderIndex + 1) % v->orders.size());
        v->path.clear();
        v->pathIndex = 0;
        v->stopAtMiddle = false;
        if (v->state == VehicleState::Loading) v->state = VehicleState::Driving;
    }
    return CommandResult::success();
}

} // namespace tp::cmd
