#include "sim/cargo.hpp"
#include "sim/industries.hpp"
#include "sim/town.hpp"
#include "sim/world.hpp"
#include "core/hash.hpp"
#include "core/math.hpp"
#include <algorithm>
#include <map>
#include <format>
#include <queue>
#include <set>

namespace tp {
namespace {
using C = CargoType;
constexpr C kNone = C::None;

// What every shop sells, whatever its town needs.
constexpr bool soldInShops(CargoType type) { return type == C::Food || type == C::Goods || type == C::Fish; }

// building, inputs, output, units a day
constexpr IndustryRecipe kRecipes[] = {
    {10, {C::Food, kNone}, C::Goods, 32},        // workshop (town)
    {11, {C::Grain, kNone}, C::Food, 32},        // factory (town)
    {12, {kNone, kNone}, C::Grain, 32},          // farm (town)
    {15, {kNone, kNone}, C::Grain, 48},          // grain farm
    {16, {kNone, kNone}, C::Wool, 32},           // sheep farm
    {17, {kNone, kNone}, C::Milk, 40},           // dairy farm
    {18, {kNone, kNone}, C::Logs, 48},           // logging camp
    {19, {kNone, kNone}, C::Stone, 56},          // quarry
    {20, {kNone, kNone}, C::IronOre, 48},        // iron mine
    {21, {kNone, kNone}, C::Coal, 56},           // coal mine
    {22, {C::Milk, kNone}, C::Cheese, 48},       // cheese dairy
    {23, {C::Wool, kNone}, C::Cloth, 40},        // textile mill
    {24, {C::Logs, kNone}, C::Planks, 56},       // sawmill
    {25, {C::Stone, kNone}, C::Bricks, 56},      // brickworks
    {26, {C::IronOre, C::Coal}, C::Steel, 48},   // steel mill
    {27, {C::Steel, C::Planks}, C::Goods, 40},   // goods factory
    {28, {C::Planks, C::Cloth}, C::Furniture, 36}, // furniture factory
    {29, {kNone, kNone}, C::Fish, 40},           // fish farm
};
} // namespace

std::span<const IndustryRecipe> industryRecipes() { return kRecipes; }

const IndustryRecipe* recipeFor(u16 buildingType) {
    for (const IndustryRecipe& r : kRecipes)
        if (r.building == buildingType) return &r;
    return nullptr;
}

std::string_view cargoName(CargoType type) {
    switch(type) {
    case CargoType::Grain: return "Grain";
    case CargoType::Food: return "Food";
    case CargoType::Goods: return "Goods";
    case CargoType::Wool: return "Wool";
    case CargoType::Milk: return "Milk";
    case CargoType::Logs: return "Logs";
    case CargoType::Stone: return "Stone";
    case CargoType::IronOre: return "Iron ore";
    case CargoType::Coal: return "Coal";
    case CargoType::Planks: return "Planks";
    case CargoType::Cheese: return "Cheese";
    case CargoType::Cloth: return "Cloth";
    case CargoType::Bricks: return "Bricks";
    case CargoType::Steel: return "Steel";
    case CargoType::Furniture: return "Furniture";
    case CargoType::Fish: return "Fish";
    default: return "Passengers";
    }
}

CargoClass cargoClass(CargoType type) {
    switch (type) {
    case CargoType::Grain: case CargoType::Stone: case CargoType::IronOre: case CargoType::Coal: return CargoClass::Bulk;
    case CargoType::Food: case CargoType::Milk: case CargoType::Cheese: case CargoType::Fish: return CargoClass::Refrigerated;
    case CargoType::Goods: case CargoType::Wool: case CargoType::Cloth: case CargoType::Furniture: return CargoClass::Piece;
    case CargoType::Logs: case CargoType::Planks: case CargoType::Bricks: case CargoType::Steel: return CargoClass::Flatbed;
    default: return CargoClass::None;
    }
}

std::string_view cargoClassName(CargoClass c) {
    switch (c) {
    case CargoClass::Bulk: return "Bulk";
    case CargoClass::Refrigerated: return "Refrigerated";
    case CargoClass::Piece: return "Piece goods";
    case CargoClass::Flatbed: return "Timber, bricks and steel";
    default: return "Passengers";
    }
}

namespace cargo {
u32 quantity(const std::vector<CargoBatch>& batches, CargoType type) {
    u32 n = 0;
    for (const auto& b : batches) if (b.type == type) n += b.quantity;
    return n;
}
u32 classQuantity(const std::vector<CargoBatch>& batches, CargoClass c) {
    u32 n = 0;
    for (const auto& b : batches) if (cargoClass(b.type) == c) n += b.quantity;
    return n;
}
bool accepts(const World& w, const Building& b, CargoType type) {
    if (const IndustryRecipe* r = recipeFor(b.type)) return type != CargoType::None && (r->inputs[0] == type || r->inputs[1] == type);
    const BuildingCategory cat = b.info().category;
    if (cat == BuildingCategory::Retail && soldInShops(type)) return true;
    // Towns take what they need at their shops, offices and civic buildings.
    if (cat == BuildingCategory::Retail || cat == BuildingCategory::Office || cat == BuildingCategory::Civic)
        if (const Town* t = w.towns.tryGet(b.town)) return towns::needs(*t, type);
    return false;
}
namespace {
using Graph = std::map<StationId, std::vector<StationId>>;
// Stations linked by vehicles that can carry a cargo, from each loading stop to the next stop.
Graph freightGraph(const World& w, CargoType type) {
    Graph graph;
    for (const auto [id, v] : w.vehicles) {
        if (v.stopped || v.cargoCapacity(type) == 0) continue;
        std::vector<Order> stops;
        for (const auto& o : v.orders)
            if (o.kind == OrderKind::Station && !o.passThrough() && w.stations.contains(o.station) &&
                w.stations[o.station].takesFreight())
                stops.push_back(o);
        // Only where the cargo may be picked up, to the next stop if it is dropped off there.
        for (usize i = 0; i < stops.size(); ++i)
            if (stops[i].loads(type) && stops[(i + 1) % stops.size()].unloads(type))
                graph[stops[i].station].push_back(stops[(i + 1) % stops.size()].station);
    }
    for (auto& [at, edges] : graph) std::sort(edges.begin(), edges.end());
    return graph;
}
// Breadth first from `from`: each reachable station mapped to the first hop towards it.
std::map<StationId, StationId> firstHops(const Graph& graph, StationId from) {
    std::queue<StationId> queue;
    std::map<StationId, StationId> first;
    first[from] = from; queue.push(from);
    while (!queue.empty()) {
        const auto at = queue.front(); queue.pop();
        const auto it = graph.find(at);
        if (it == graph.end()) continue;
        for (const auto next : it->second) {
            if (first.contains(next)) continue;
            first[next] = at == from ? next : first[at];
            queue.push(next);
        }
    }
    return first;
}
}
StationId nextHop(const World& w, StationId from, StationId to, CargoType type) {
    if (from == to) return to;
    const auto first = firstHops(freightGraph(w, type), from);
    const auto it = first.find(to);
    return it == first.end() || to == from ? StationId{} : it->second;
}
namespace {
constexpr u32 kStockLimit = 4096;
const Building* building(const World& w, BuildingRef ref) {
    const auto* b = w.buildings.tryGet(ref.id);
    return b && b->serial == ref.serial ? b : nullptr;
}
bool covered(const Station& s, const Building& b) {
    const i32 size = b.size();
    for (const auto tile : s.tiles) {
        const i32 dx = std::max({0, b.pos.x - tile.x, tile.x - (b.pos.x + size - 1)});
        const i32 dy = std::max({0, b.pos.y - tile.y, tile.y - (b.pos.y + size - 1)});
        if (std::max(dx, dy) <= s.catchment) return true;
    }
    return false;
}
bool registers(const World& w, const Building& b) {
    const BuildingCategory cat = b.info().category;
    if (cat == BuildingCategory::Industrial || cat == BuildingCategory::Retail) return true;
    return w.settings.industries && (cat == BuildingCategory::Office || cat == BuildingCategory::Civic);
}
}
bool covers(const World& w, StationId station, const Building& b) {
    const Station* s = w.stations.tryGet(station);
    return s && covered(*s, b);
}
void daily(World& w) {
    std::erase_if(w.industries, [&](const Industry& i) { return !building(w, i.building); });
    std::set<std::pair<u32, u32>> registered;
    for (const auto& i : w.industries) registered.emplace(i.building.id.value, i.building.serial);
    for (const auto [id, b] : w.buildings) {
        if (!registers(w, b) || registered.contains({id.value, b.serial})) continue;
        Industry entry{b.ref(id)};
        // Countryside producers start at a production level of their own.
        if (b.type > 14) entry.level = static_cast<u8>(1 + hashCoords(w.settings.seed, b.pos.x, b.pos.y, 0x1D57) % 4);
        w.industries.push_back(entry);
    }
    for (auto& i : w.industries) {
        const auto& b = *building(w, i.building);
        if (const IndustryRecipe* r = recipeFor(b.type)) {
            const usize out = static_cast<usize>(r->output);
            u32 n = kStockLimit - std::min(kStockLimit, i.stock[out]);
            if (r->inputs[0] == CargoType::None) {
                n = std::min<u32>(n, r->rate * i.level / 2);
            } else {
                n = std::min<u32>(n, r->rate * i.level / 2);
                for (const CargoType in : r->inputs)
                    if (in != CargoType::None) n = std::min(n, i.stock[static_cast<usize>(in)]);
                for (const CargoType in : r->inputs)
                    if (in != CargoType::None) i.stock[static_cast<usize>(in)] -= n;
            }
            i.stock[out] += n;
            i.producedThisMonth += n;
        }
        if (b.info().category == BuildingCategory::Retail)
            for (const CargoType c : {CargoType::Food, CargoType::Goods, CargoType::Fish}) i.stock[static_cast<usize>(c)] -= std::min(i.stock[static_cast<usize>(c)], 16u);
    }
    for (auto [sid, station] : w.stations) {
        std::erase_if(station.cargoWaiting, [&](const CargoBatch& batch) {
            return !building(w, batch.receiver) || !w.stations.contains(batch.destination) ||
                   w.clock.tick - batch.producedTick > 30 * kTicksPerDay;
        });
    }
    // Dispatch only when a complete freight service exists. Inventories stay at the provider
    // otherwise. Route graphs are built once per cargo and reachability once per station.
    std::map<CargoType, Graph> graphs;
    std::map<std::pair<CargoType, StationId>, std::map<StationId, StationId>> reach;
    for (auto& source : w.industries) {
        const auto& b = *building(w, source.building);
        const IndustryRecipe* recipe = recipeFor(b.type);
        if (!recipe) continue;
        const CargoType type = recipe->output;
        auto& stock = source.stock[static_cast<usize>(type)];
        if (!stock) continue;
        bool sent = false;
        for (const auto [sid, s] : w.stations) {
            u32 waiting = 0;
            for (const auto& batch : s.cargoWaiting) waiting += batch.quantity;
            if (!s.takesFreight() || !covered(s, b) || s.cargoWaiting.size() >= 256 || waiting >= s.waitingCapacity()) continue;
            if (!graphs.contains(type)) graphs.emplace(type, freightGraph(w, type));
            const auto key = std::pair{type, sid};
            if (!reach.contains(key)) reach.emplace(key, firstHops(graphs[type], sid));
            const auto& reachable = reach[key];
            if (reachable.size() <= 1) continue;
            for (const auto& dest : w.industries) {
                if (dest.building == source.building) continue;
                const auto& target = *building(w, dest.building);
                if (!accepts(w, target, type)) continue;
                for (const auto [tid, t] : w.stations) {
                    if (tid == sid || !t.takesFreight() || !reachable.contains(tid) || !covered(t, target)) continue;
                    const u32 dispatch = std::max<u32>(128, recipe->rate * source.level / 2);
                    const u32 n = std::min({stock, dispatch, static_cast<u32>(s.waitingCapacity()) - waiting});
                    w.stations[sid].cargoWaiting.push_back({type, n, sid, tid, dest.building, w.clock.tick, source.building});
                    stock -= n; source.shippedThisMonth += n; sent = true; break;
                }
                if (sent) break;
            }
            if (sent) break;
        }
    }
}
void monthly(World& w) {
    for (auto& i : w.industries) {
        const Building* b = building(w, i.building);
        const IndustryRecipe* r = b ? recipeFor(b->type) : nullptr;
        // Count completed exports, never cargo merely waiting at a station. Require a
        // useful share of full capacity so input-starved plants cannot grow on a trickle.
        if (r && i.level < 8) {
            const u32 capacity = static_cast<u32>(r->rate) * i.level / 2 * 30;
            const u32 utilisation = static_cast<u32>(std::min<u64>(100, static_cast<u64>(i.exportedThisMonth) * 100 / std::max(1u, capacity)));
            if (utilisation >= 50) i.growthProgress += static_cast<u16>(utilisation / 4);
            else i.growthProgress = static_cast<u16>(i.growthProgress > 5 ? i.growthProgress - 5 : 0);
            if (i.growthProgress >= 100) {
                i.growthProgress -= 100;
                ++i.level;
                Building& grown = w.buildings[i.building.id];
                if (industries::isCountryside(grown.type)) {
                    grown.variant = static_cast<u8>((i.level - 1) / 3);
                    w.map.touch(grown.pos);
                }
                w.addNews(NewsKind::General, std::format("{} has expanded to production level {}", b->info().name, i.level), b->pos);
            }
        }
        i.exportedLastMonth = i.exportedThisMonth;
        i.exportedThisMonth = 0;
        i.producedLastMonth = i.producedThisMonth;
        i.shippedLastMonth = i.shippedThisMonth;
        i.producedThisMonth = i.shippedThisMonth = 0;
    }
}
i32 exchangeAtStop(World& w, VehicleId vid, StationId sid) {
    auto& v = w.vehicles[vid];
    auto* s = w.stations.tryGet(sid);
    if (!s || !s->takesFreight()) return 0;
    const auto* order = v.currentOrder();
    if (order && order->passThrough()) return 0;
    StationId next;
    for (usize k = 1; k < v.orders.size(); ++k) {
        const auto& o = v.orders[(v.orderIndex + k) % v.orders.size()];
        if (o.kind == OrderKind::Station && !o.passThrough()) { next = o.station; break; }
    }
    i32 moved = 0;
    CargoCounts loaded, unloaded;
    const auto finish = [&]() {
        if (moved) {
            v.cargoExchangeTick = w.clock.tick; v.cargoExchangeUnits = static_cast<u32>(moved);
            v.cargoLoaded = loaded; v.cargoUnloaded = unloaded;
        }
        return moved;
    };
    usize waitingUnits = 0;
    for (const auto& batch : s->cargoWaiting) waitingUnits += batch.quantity;
    std::vector<CargoBatch> remaining;
    for (const auto& batch : v.cargo) {
        if (order && !order->unloads(batch.type)) {
            remaining.push_back(batch);
        } else if (batch.destination == sid) {
            auto it = std::ranges::find_if(w.industries, [&](const Industry& i) { return i.building == batch.receiver; });
            const auto* target = building(w, batch.receiver);
            if (it != w.industries.end() && target && accepts(w, *target, batch.type) && covered(*s, *target)) {
                const IndustryRecipe* r = recipeFor(target->type);
                const bool input = r && (r->inputs[0] == batch.type || r->inputs[1] == batch.type);
                const bool shop = target->info().category == BuildingCategory::Retail && soldInShops(batch.type);
                u32 delivered = batch.quantity;
                if (input || shop) {
                    auto& stock = it->stock[static_cast<usize>(batch.type)];
                    delivered = std::min(batch.quantity, kStockLimit - std::min(kStockLimit, stock));
                    stock += delivered;
                }
                it->delivered += delivered;
                if (auto source = std::ranges::find_if(w.industries, [&](const Industry& i) { return i.building == batch.producer; }); source != w.industries.end())
                    source->exportedThisMonth += delivered;
                // Deliveries to town consumers drive growth; factory inputs grow the factory.
                if (!r)
                    if (Town* town = w.towns.tryGet(target->town)) town->goodsReceived += delivered;
                // A town counts the deliveries it needs towards its growth.
                if (Town* town = w.towns.tryGet(target->town); town && towns::needs(*town, batch.type))
                    town->supplied[static_cast<usize>(batch.type)] += delivered;
                const auto* origin = w.stations.tryGet(batch.origin);
                const i64 distance = origin ? manhattan(s->sign.x - origin->sign.x, s->sign.y - origin->sign.y) : 0;
                const Money pay = subsidies::cargoPay(w, v.owner, batch, *target, w.fareIncome(delivered * (distance + 10) / 4));
                const Expense cat = v.info().type == VehicleType::Rail ? Expense::TrainIncome :
                    v.info().type == VehicleType::Ship ? Expense::ShipIncome : Expense::RoadVehicleIncome;
                if (auto* co = w.companies.tryGet(v.owner)) co->earn(cat, pay);
                v.profitThisYear += pay;
            }
            moved += static_cast<i32>(batch.quantity);
            unloaded[static_cast<usize>(batch.type)] += batch.quantity;
        } else if ((order && (order->flags & kOrderUnloadOnly)) || (nextHop(w, sid, batch.destination, batch.type).valid() &&
                   nextHop(w, sid, batch.destination, batch.type) != next)) {
            const u32 space = waitingUnits < s->waitingCapacity() && s->cargoWaiting.size() < 256 ?
                static_cast<u32>(s->waitingCapacity() - waitingUnits) : 0;
            const u32 n = std::min(batch.quantity, space);
            if (n) {
                auto transferred = batch; transferred.quantity = n;
                s->cargoWaiting.push_back(transferred); waitingUnits += n; moved += static_cast<i32>(n);
                unloaded[static_cast<usize>(batch.type)] += n;
            }
            if (n < batch.quantity) { auto retained = batch; retained.quantity -= n; remaining.push_back(retained); }
        } else remaining.push_back(batch);
    }
    v.cargo = std::move(remaining);
    if (order && (order->flags & (kOrderNoLoading | kOrderUnloadOnly))) return finish();
    if (!next.valid()) return finish();
    for (auto& batch : s->cargoWaiting) {
        if (!batch.quantity || (order && !order->loads(batch.type)) || nextHop(w, sid, batch.destination, batch.type) != next) continue;
        // The vehicle's space for this class of cargo is shared by everything of the class.
        const u32 cap = v.cargoCapacity(batch.type), load = classQuantity(v.cargo, cargoClass(batch.type));
        const u32 n = std::min(batch.quantity, cap > load ? cap - load : 0);
        if (!n) continue;
        auto part = batch; part.quantity = n;
        v.cargo.push_back(part); batch.quantity -= n; moved += static_cast<i32>(n);
        loaded[static_cast<usize>(batch.type)] += n;
    }
    std::erase_if(s->cargoWaiting, [](const CargoBatch& b) { return b.quantity == 0; });
    return finish();
}
}
}
