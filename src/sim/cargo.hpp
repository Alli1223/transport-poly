#pragma once
#include "sim/buildings.hpp"
#include <array>
#include <string_view>
#include <vector>

namespace tp {
enum class CargoType : u8 { Grain, Food, Goods, Count, None = 255 };
inline constexpr usize kCargoTypes = 3;
std::string_view cargoName(CargoType type);
struct CargoBatch {
    CargoType type = CargoType::Grain;
    u32 quantity = 0;
    StationId origin, destination;
    BuildingRef receiver;
    i64 producedTick = 0;
    template<class Ar> void serialize(Ar& ar) { ar(type, quantity, origin, destination, receiver, producedTick); }
};
struct Industry {
    BuildingRef building;
    std::array<u32, kCargoTypes> stock{};
    u64 delivered = 0;
    template<class Ar> void serialize(Ar& ar) { ar(building, stock, delivered); }
};
class World;
namespace cargo {
void daily(World& w);
// Deterministic freight graph, filtered by cargo compatibility and operating vehicles.
StationId nextHop(const World& w, StationId from, StationId to, CargoType type);
i32 exchangeAtStop(World& w, VehicleId vehicle, StationId station);
u32 quantity(const std::vector<CargoBatch>& batches, CargoType type);
bool accepts(const Building& b, CargoType type);
}
}
