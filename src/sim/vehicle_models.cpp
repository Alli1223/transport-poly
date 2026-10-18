#include "sim/vehicle.hpp"

#include <array>

namespace tp {

namespace {

using V = VehicleType;

// name                  type     cap  km/h len  price    running intro life visual powered
constexpr std::array<VehicleModel, 29> kModels = {{
    {"Albatross Bus",     V::Road, 31,  56,  10, 95'000,  28'000, 1930, 15,  0},
    {"Comet Bus",         V::Road, 35,  72,  10, 110'000, 30'000, 1962, 15,  1},
    {"Leopard Bus",       V::Road, 37,  90,  11, 135'000, 34'000, 1978, 15,  2},
    {"Foster Bendy Bus",  V::Road, 62,  96,  15, 180'000, 45'000, 1994, 15,  3},
    {"Tramcar Mk1",       V::Tram, 70,  50,  14, 250'000, 50'000, 1930, 25,  0},
    {"Tramcar Mk2",       V::Tram, 110, 70,  18, 320'000, 60'000, 1968, 25,  1},
    {"Low-floor Tram",    V::Tram, 160, 80,  22, 420'000, 70'000, 1995, 25,  2},
    {"Kirby Tank Engine", V::Rail, 0,   64,  10, 160'000, 36'000, 1925, 30,  0, true},
    {"Ploddyphut Diesel", V::Rail, 0,   112, 12, 320'000, 52'000, 1952, 30,  1, true},
    {"Highball Express",  V::Rail, 0,   160, 13, 480'000, 70'000, 1975, 30,  2, true},
    {"Sprinter DMU",      V::Rail, 70,  120, 12, 260'000, 40'000, 1980, 30,  3, true},
    {"Wooden Coach",      V::Rail, 36,  96,  10, 45'000,  6'000,  1925, 30,  8},
    {"Steel Coach",       V::Rail, 48,  160, 11, 70'000,  8'000,  1958, 30,  9},
    {"Harbour Launch",    V::Ship, 40,  30,  12, 90'000,  12'000, 1920, 25,  0},
    {"Coastal Ferry",     V::Ship, 140, 38,  20, 260'000, 32'000, 1948, 30,  1},
    {"Swiftwing Hydrofoil", V::Ship, 100, 72, 16, 450'000, 56'000, 1974, 25, 2},
    {"Grain Lorry", V::Road, 0, 64, 11, 85'000, 20'000, 1930, 15, 4, false, CargoType::Grain, 30, true},
    {"Food Van", V::Road, 0, 72, 11, 100'000, 22'000, 1930, 15, 5, false, CargoType::Food, 25},
    {"Goods Lorry", V::Road, 0, 80, 12, 120'000, 25'000, 1930, 15, 6, false, CargoType::Goods, 35},
    {"Grain Hopper", V::Rail, 0, 100, 10, 50'000, 6'000, 1925, 30, 10, false, CargoType::Grain, 50, true},
    {"Refrigerated Wagon", V::Rail, 0, 120, 11, 65'000, 7'000, 1925, 30, 11, false, CargoType::Food, 40},
    {"Goods Box Wagon", V::Rail, 0, 120, 11, 60'000, 6'500, 1925, 30, 12, false, CargoType::Goods, 45},
    {"Bulk Coaster", V::Ship, 0, 30, 22, 280'000, 30'000, 1920, 30, 3, false, CargoType::Grain, 250, true},
    {"Refrigerated Coaster", V::Ship, 0, 32, 22, 300'000, 32'000, 1920, 30, 4, false, CargoType::Food, 200},
    {"Container Coaster", V::Ship, 0, 36, 24, 360'000, 36'000, 1940, 30, 5, false, CargoType::Goods, 240},
    {"Continental Electric", V::Rail, 0, 200, 13, 620'000, 80'000, 1985, 35, 2, true},
    {"Intercity Coach", V::Rail, 64, 200, 13, 110'000, 10'000, 1985, 35, 9},
    {"Regional EMU", V::Rail, 100, 160, 13, 380'000, 48'000, 1990, 30, 3, true},
    {"Heavy Freight Diesel", V::Rail, 0, 120, 14, 420'000, 62'000, 1960, 35, 1, true},
}};

} // namespace

std::span<const VehicleModel> vehicleModels() { return kModels; }

const VehicleModel& vehicleModel(u16 index) { return kModels[index < kModels.size() ? index : 0]; }

std::string_view vehicleTypeName(VehicleType t) {
    switch (t) {
    case VehicleType::Road: return "Bus";
    case VehicleType::Tram: return "Tram";
    case VehicleType::Rail: return "Train";
    case VehicleType::Ship: return "Ship";
    }
    return "Vehicle";
}

} // namespace tp
