#include "sim/cargo.hpp"
#include "sim/world.hpp"
#include "core/math.hpp"
#include <algorithm>
#include <map>
#include <queue>
#include <set>

namespace tp {
std::string_view cargoName(CargoType type) {
    switch(type) {
    case CargoType::Grain: return "Grain";
    case CargoType::Food: return "Food";
    case CargoType::Goods: return "Goods";
    default: return "Passengers";
    }
}
namespace cargo {
u32 quantity(const std::vector<CargoBatch>& batches, CargoType type) {
    u32 n = 0;
    for (const auto& b : batches) if (b.type == type) n += b.quantity;
    return n;
}
bool accepts(const Building& b, CargoType type) {
    return (b.type == 11 && type == CargoType::Grain) ||
           (b.type == 10 && type == CargoType::Food) ||
           (b.info().category == BuildingCategory::Retail && (type == CargoType::Food || type == CargoType::Goods));
}
StationId nextHop(const World& w, StationId from, StationId to, CargoType type) {
    if (from == to) return to;
    std::map<StationId, std::vector<StationId>> graph;
    for (const auto [id, v] : w.vehicles) {
        if (v.stopped || v.cargoCapacity(type) == 0) continue;
        std::vector<Order> stops;
        for (const auto& o : v.orders)
            if (o.kind == OrderKind::Station && !o.passThrough() && w.stations.contains(o.station)) stops.push_back(o);
        for (usize i = 0; i < stops.size(); ++i)
            if (!(stops[i].flags & (kOrderNoLoading | kOrderUnloadOnly)))
                graph[stops[i].station].push_back(stops[(i + 1) % stops.size()].station);
    }
    std::queue<StationId> queue;
    std::map<StationId, StationId> first;
    first[from] = from; queue.push(from);
    while (!queue.empty()) {
        const auto at = queue.front(); queue.pop();
        auto& edges = graph[at];
        std::sort(edges.begin(), edges.end());
        for (const auto next : edges) {
            if (first.contains(next)) continue;
            first[next] = at == from ? next : first[at];
            if (next == to) return first[next];
            queue.push(next);
        }
    }
    return {};
}
namespace {
constexpr u32 kStockLimit = 4096;
const Building* building(const World& w, BuildingRef ref) {
    const auto* b = w.buildings.tryGet(ref.id);
    return b && b->serial == ref.serial ? b : nullptr;
}
bool covered(const Station& s, const Building& b) {
    for (const auto tile : s.tiles)
        if (std::max(std::abs(tile.x - b.pos.x), std::abs(tile.y - b.pos.y)) <= s.catchment) return true;
    return false;
}
}
void daily(World& w) {
    std::erase_if(w.industries, [&](const Industry& i) { return !building(w, i.building); });
    std::set<std::pair<u32, u32>> registered;
    for (const auto& i : w.industries) registered.emplace(i.building.id.value, i.building.serial);
    for (const auto [id, b] : w.buildings) {
        if (b.info().category != BuildingCategory::Industrial && b.info().category != BuildingCategory::Retail) continue;
        if (!registered.contains({id.value, b.serial}))
            w.industries.push_back(Industry{b.ref(id)});
    }
    for (auto& i : w.industries) {
        const auto& b = *building(w, i.building);
        if (b.type == 12) i.stock[0] = std::min(kStockLimit, i.stock[0] + 32);
        if (b.type == 11 || b.type == 10) {
            const usize in = b.type == 11 ? 0 : 1, out = in + 1;
            const u32 n = std::min({i.stock[in], 32u, kStockLimit - i.stock[out]});
            i.stock[in] -= n; i.stock[out] += n;
        }
        if (b.info().category == BuildingCategory::Retail)
            for (usize c = 1; c < kCargoTypes; ++c) i.stock[c] -= std::min(i.stock[c], 16u);
    }
    for (auto [sid, station] : w.stations) {
        std::erase_if(station.cargoWaiting, [&](const CargoBatch& batch) {
            return !building(w, batch.receiver) || !w.stations.contains(batch.destination) ||
                   w.clock.tick - batch.producedTick > 30 * kTicksPerDay;
        });
    }
    // Dispatch only when a complete freight service exists. Inventories stay at the provider otherwise.
    for (auto& source : w.industries) {
        const auto& b = *building(w, source.building);
        const CargoType type = b.type == 12 ? CargoType::Grain : b.type == 11 ? CargoType::Food : b.type == 10 ? CargoType::Goods : CargoType::None;
        if (type == CargoType::None) continue;
        auto& stock = source.stock[static_cast<usize>(type)];
        if (!stock) continue;
        bool sent = false;
        for (const auto [sid, s] : w.stations) {
            u32 waiting = 0;
            for (const auto& batch : s.cargoWaiting) waiting += batch.quantity;
            if (!covered(s, b) || s.cargoWaiting.size() >= 256 || waiting >= s.waitingCapacity()) continue;
            for (const auto& dest : w.industries) {
                const auto& target = *building(w, dest.building);
                if (dest.building == source.building || !accepts(target, type)) continue;
                for (const auto [tid, t] : w.stations) {
                    if (tid == sid || !covered(t, target) || !nextHop(w, sid, tid, type).valid()) continue;
                    const u32 n = std::min({stock, 128u, static_cast<u32>(s.waitingCapacity()) - waiting});
                    w.stations[sid].cargoWaiting.push_back({type, n, sid, tid, dest.building, w.clock.tick});
                    stock -= n; sent = true; break;
                }
                if (sent) break;
            }
            if (sent) break;
        }
    }
}
i32 exchangeAtStop(World& w, VehicleId vid, StationId sid) {
    auto& v = w.vehicles[vid];
    auto* s = w.stations.tryGet(sid);
    if (!s) return 0;
    const auto* order = v.currentOrder();
    if (order && order->passThrough()) return 0;
    StationId next;
    for (usize k = 1; k < v.orders.size(); ++k) {
        const auto& o = v.orders[(v.orderIndex + k) % v.orders.size()];
        if (o.kind == OrderKind::Station && !o.passThrough()) { next = o.station; break; }
    }
    i32 moved = 0;
    std::array<u32, kCargoTypes> loaded{}, unloaded{};
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
        if (batch.destination == sid) {
            auto it = std::ranges::find_if(w.industries, [&](const Industry& i) { return i.building == batch.receiver; });
            const auto* target = building(w, batch.receiver);
            if (it != w.industries.end() && target && accepts(*target, batch.type) && covered(*s, *target)) {
                auto& stock = it->stock[static_cast<usize>(batch.type)];
                const u32 delivered = std::min(batch.quantity, kStockLimit - stock);
                stock += delivered; it->delivered += delivered;
                const auto* origin = w.stations.tryGet(batch.origin);
                const i64 distance = origin ? manhattan(s->sign.x - origin->sign.x, s->sign.y - origin->sign.y) : 0;
                const Money pay = w.fareIncome(delivered * (distance + 10) / 4);
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
        if (!batch.quantity || nextHop(w, sid, batch.destination, batch.type) != next) continue;
        const u32 cap = v.cargoCapacity(batch.type), load = quantity(v.cargo, batch.type);
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
