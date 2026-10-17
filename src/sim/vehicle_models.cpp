#include "sim/vehicle.hpp"

#include <array>

namespace tp {

namespace {

using V = VehicleType;

// name                  type     cap  km/h len  price    running intro life visual
constexpr std::array<VehicleModel, 7> kModels = {{
    {"Albatross Bus",     V::Road, 31,  56,  10, 95'000,  28'000, 1930, 15,  0},
    {"Comet Bus",         V::Road, 35,  72,  10, 110'000, 30'000, 1962, 15,  1},
    {"Leopard Bus",       V::Road, 37,  90,  11, 135'000, 34'000, 1978, 15,  2},
    {"Foster Bendy Bus",  V::Road, 62,  96,  15, 180'000, 45'000, 1994, 15,  3},
    {"Tramcar Mk1",       V::Tram, 70,  50,  14, 250'000, 50'000, 1930, 25,  0},
    {"Tramcar Mk2",       V::Tram, 110, 70,  18, 320'000, 60'000, 1968, 25,  1},
    {"Low-floor Tram",    V::Tram, 160, 80,  22, 420'000, 70'000, 1995, 25,  2},
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
