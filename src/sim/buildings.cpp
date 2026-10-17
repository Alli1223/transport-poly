#include "sim/buildings.hpp"

#include <array>

namespace tp {

namespace {

using C = BuildingCategory;
using T = TownClass;

constexpr std::array<BuildingType, 15> kTypes = {{
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
}};

} // namespace

std::span<const BuildingType> buildingTypes() { return kTypes; }

const BuildingType& buildingType(u16 index) { return kTypes[index < kTypes.size() ? index : 0]; }

} // namespace tp
