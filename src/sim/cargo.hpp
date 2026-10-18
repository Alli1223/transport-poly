#pragma once
#include "sim/buildings.hpp"
#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace tp {
// Freight. Grain, food and goods were the first chain (town farms, factories and workshops);
// the rest come from countryside industries and the processing plants that use them.
enum class CargoType : u8 {
    Grain, Food, Goods,
    Wool, Milk, Logs, Stone, IronOre, Coal,           // raw materials
    Planks, Cheese, Cloth, Bricks, Steel, Furniture,  // made from them
    Count, None = 255
};
inline constexpr usize kCargoTypes = static_cast<usize>(CargoType::Count);
inline constexpr usize kOriginalCargoTypes = 3; // saves before version 21 know only these
std::string_view cargoName(CargoType type);

// Each cargo travels in one class of wagon, lorry or hold: hoppers take bulk, refrigerated vans
// take milk and food, box vans take piece goods and flatbeds take timber, bricks and steel.
// A vehicle's capacity is shared by the cargo of its class.
enum class CargoClass : u8 { Bulk, Refrigerated, Piece, Flatbed, None };
CargoClass cargoClass(CargoType type);
std::string_view cargoClassName(CargoClass c);

// Amounts per cargo type, saved with as many types as the save version knew.
struct CargoCounts {
    std::array<u32, kCargoTypes> n{};
    u32& operator[](usize i) { return n[i]; }
    u32 operator[](usize i) const { return n[i]; }
    friend bool operator==(const CargoCounts&, const CargoCounts&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        if (ar.version == 0 || ar.version >= 21) {
            ar(n);
        } else {
            std::array<u32, kOriginalCargoTypes> old{};
            if constexpr (!Ar::kReading)
                for (usize i = 0; i < old.size(); ++i) old[i] = n[i];
            ar(old);
            if constexpr (Ar::kReading) {
                n = {};
                for (usize i = 0; i < old.size(); ++i) n[i] = old[i];
            }
        }
    }
};

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
    CargoCounts stock;
    u64 delivered = 0;
    u8 level = 2;           // production level 1-4 (raw producers make rate * level / 2 a day)
    u32 producedThisMonth = 0, producedLastMonth = 0;
    u32 shippedThisMonth = 0, shippedLastMonth = 0; // handed to stations
    template<class Ar> void serialize(Ar& ar) {
        ar(building, stock, delivered);
        if (ar.version == 0 || ar.version >= 21) ar(level, producedThisMonth, producedLastMonth, shippedThisMonth, shippedLastMonth);
    }
};

// What an industry building makes: raw producers have no inputs; processing plants need all of
// theirs and make up to `rate` units a day from them.
struct IndustryRecipe {
    u16 building;
    std::array<CargoType, 2> inputs;
    CargoType output;
    u16 rate;
};
std::span<const IndustryRecipe> industryRecipes();
const IndustryRecipe* recipeFor(u16 buildingType);

class World;
namespace cargo {
void daily(World& w);
void monthly(World& w);
// Deterministic freight graph, filtered by cargo compatibility and operating vehicles.
StationId nextHop(const World& w, StationId from, StationId to, CargoType type);
i32 exchangeAtStop(World& w, VehicleId vehicle, StationId station);
u32 quantity(const std::vector<CargoBatch>& batches, CargoType type);
// Everything of one class aboard (it shares the class's capacity).
u32 classQuantity(const std::vector<CargoBatch>& batches, CargoClass c);
// Whether a building takes a cargo: an industry's inputs, food and goods at shops, and what a
// town needs at its shops, offices and civic buildings.
bool accepts(const World& w, const Building& b, CargoType type);
// Whether a station's catchment reaches any tile of a building.
bool covers(const World& w, StationId station, const Building& b);
}
}
