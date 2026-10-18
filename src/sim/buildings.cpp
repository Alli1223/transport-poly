#include "sim/buildings.hpp"

#include <array>

namespace tp {

namespace {

using C = BuildingCategory;
using T = TownClass;

constexpr std::array<BuildingType, 29> kTypes = {{
    // name                 category        size res  jobs cust  minClass    zones                        weight var
    {"Cottage",             C::Residential, 1,   3,   0,   0,    T::Village, kZoneInner | kZoneOuter,       14,   3},
    {"House",               C::Residential, 1,   5,   0,   0,    T::Village, kZoneCore | kZoneInner | kZoneOuter, 16, 3},
    {"Terraced houses",     C::Residential, 1,   10,  0,   0,    T::Town,    kZoneCore | kZoneInner,        10,   2},
    {"Apartments",          C::Residential, 1,   28,  0,   0,    T::Town,    kZoneCore | kZoneInner,        6,    2},
    {"Tower block",         C::Residential, 1,   90,  0,   0,    T::City,    kZoneCore,                     4,    2},
    {"Corner shop",         C::Retail,      1,   0,   4,   40,   T::Village, kZoneCore | kZoneInner,        4,    2},
    {"High street shops",   C::Retail,      1,   0,   12,  140,  T::Town,    kZoneCore,                     5,    2},
    {"Department store",    C::Retail,      2,   0,   70,  700,  T::City,    kZoneCore,                     2,    1},
    {"Small offices",       C::Office,      1,   0,   30,  0,    T::Town,    kZoneCore | kZoneInner,        5,    2},
    {"Office tower",        C::Office,      1,   0,   160, 0,    T::City,    kZoneCore,                     3,    2},
    {"Workshop",            C::Industrial,  1,   0,   20,  0,    T::Village, kZoneOuter,                    5,    2},
    {"Factory",             C::Industrial,  2,   0,   90,  0,    T::Town,    kZoneOuter,                    2,    1},
    {"Farm",                C::Industrial,  1,   2,   6,   0,    T::Village, kZoneOuter,                    3,    1},
    {"Church",              C::Civic,       1,   0,   3,   0,    T::Village, kZoneCore | kZoneInner,        1,    1},
    {"School",              C::Civic,       1,   0,   15,  0,    T::Town,    kZoneInner,                    2,    1},
    // Countryside industries: never built by towns (no zones), placed on their own sites.
    {"Grain farm",          C::Industrial,  3,   0,   12,  0,    T::Village, 0,                             0,    1},
    {"Sheep farm",          C::Industrial,  3,   0,   8,   0,    T::Village, 0,                             0,    1},
    {"Dairy farm",          C::Industrial,  3,   0,   10,  0,    T::Village, 0,                             0,    1},
    {"Logging camp",        C::Industrial,  2,   0,   10,  0,    T::Village, 0,                             0,    1},
    {"Quarry",              C::Industrial,  3,   0,   16,  0,    T::Village, 0,                             0,    1},
    {"Iron mine",           C::Industrial,  2,   0,   20,  0,    T::Village, 0,                             0,    1},
    {"Coal mine",           C::Industrial,  2,   0,   20,  0,    T::Village, 0,                             0,    1},
    {"Cheese dairy",        C::Industrial,  2,   0,   18,  0,    T::Village, 0,                             0,    1},
    {"Textile mill",        C::Industrial,  2,   0,   24,  0,    T::Village, 0,                             0,    1},
    {"Sawmill",             C::Industrial,  2,   0,   16,  0,    T::Village, 0,                             0,    1},
    {"Brickworks",          C::Industrial,  2,   0,   18,  0,    T::Village, 0,                             0,    1},
    {"Steel mill",          C::Industrial,  3,   0,   40,  0,    T::Village, 0,                             0,    1},
    {"Goods factory",       C::Industrial,  3,   0,   36,  0,    T::Village, 0,                             0,    1},
    {"Furniture factory",   C::Industrial,  2,   0,   22,  0,    T::Village, 0,                             0,    1},
}};

} // namespace

std::span<const BuildingType> buildingTypes() { return kTypes; }

const BuildingType& buildingType(u16 index) { return kTypes[index < kTypes.size() ? index : 0]; }

} // namespace tp
