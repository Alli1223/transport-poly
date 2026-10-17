#pragma once
// Town buildings: the type catalogue and per-instance state.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

#include <span>
#include <string_view>

namespace tp {

enum class BuildingCategory : u8 { Residential, Retail, Office, Industrial, Civic };

enum class TownClass : u8 { Village, Town, City };

struct BuildingType {
    std::string_view name;
    BuildingCategory category;
    u8 size;          // footprint is size x size tiles
    u16 residents;    // homes
    u16 jobs;         // work places
    u16 customers;    // shoppers it can serve (retail only)
    TownClass minClass;
    u8 zones;         // bit 0 = core, bit 1 = inner, bit 2 = outskirts
    u16 weight;       // relative selection weight
    u8 variants;      // number of visual variants
};

inline constexpr u8 kZoneCore = 1;
inline constexpr u8 kZoneInner = 2;
inline constexpr u8 kZoneOuter = 4;

std::span<const BuildingType> buildingTypes();
const BuildingType& buildingType(u16 index);

// Reference to a building that stays safe when the building is demolished and its id reused.
struct BuildingRef {
    BuildingId id;
    u32 serial = 0;

    bool valid() const { return id.valid(); }
    friend bool operator==(const BuildingRef&, const BuildingRef&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(id, serial);
    }
};

struct Building {
    u32 serial = 0;
    u16 type = 0;
    TownId town;
    TilePos pos;        // north-west tile of the footprint
    Dir facing = Dir::S; // side the entrance faces (towards the road)
    u8 variant = 0;
    u16 residents = 0;  // people living here
    u16 workers = 0;    // people employed here
    u16 customers = 0;  // people who shop here
    i64 builtTick = 0;

    const BuildingType& info() const { return buildingType(type); }
    BuildingRef ref(BuildingId id) const { return {id, serial}; }
    // Centre of the footprint in tile units (x2 to stay integral).
    TilePos centre2() const {
        const i32 s = info().size;
        return {pos.x * 2 + s, pos.y * 2 + s};
    }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(serial, type, town, pos, facing, variant, residents, workers, customers, builtTick);
    }
};

} // namespace tp
