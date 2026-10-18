#pragma once
// Stations (bus / tram stops, rail platforms, docks) and vehicle depots.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/cargo.hpp"
#include "sim/ids.hpp"
#include "sim/track.hpp"

#include <map>
#include <string>
#include <vector>

namespace tp {

class World;

enum class StopKind : u8 { Bus, Tram, Rail, Dock };

enum StationFacility : u8 {
    kFacilityBus = 1 << 0,
    kFacilityTram = 1 << 1,
    kFacilityRail = 1 << 2,
    kFacilityDock = 1 << 3,
};

// Catchment radius (tiles, Chebyshev) of a stop kind.
constexpr i32 catchmentRadius(StopKind k) {
    switch (k) {
    case StopKind::Bus: return 3;
    case StopKind::Tram: return 3;
    case StopKind::Rail: return 4;
    case StopKind::Dock: return 4;
    }
    return 3;
}

struct StationEntrance {
    TilePos road;
    std::vector<TilePos> tiles; // reserved approach path, the first tile holds the entrance building
    std::vector<TPoint> path; // road pavement to platform
    TrackEdgeId platform;
    template<class Ar> void serialize(Ar& ar) { ar(road,tiles,path,platform); }
};

struct Station {
    std::vector<StationEntrance> entrances;
    std::string name;
    CompanyId owner;
    TownId town;
    TilePos sign;               // tile the name label is shown at
    std::vector<TilePos> tiles; // all tiles belonging to the station
    u8 facilities = 0;
    i32 catchment = 3;
    i64 builtTick = 0;
    // Passengers waiting here, grouped by the station they want to travel to next.
    std::map<StationId, std::vector<PersonId>> waiting;
    std::vector<CargoBatch> cargoWaiting;
    i64 lastVisitTick = -1; // last time a vehicle stopped here
    i32 boardedThisMonth = 0;
    i32 boardedLastMonth = 0;
    i32 rating = 175;       // 0-255, OpenTTD style service rating

    std::vector<TilePos> catchmentTiles() const {
        if ((facilities & kFacilityRail) && !entrances.empty()) {
            std::vector<TilePos> out; for (const auto& entry : entrances) out.push_back(entry.road); return out;
        }
        return tiles;
    }
    usize waitingCount() const {
        usize n = 0;
        for (const auto& [to, list] : waiting) n += list.size();
        return n;
    }
    usize waitingCapacity() const {
        const usize perTile = (facilities & kFacilityRail) ? 512 : (facilities & kFacilityDock) ? 256 :
                              (facilities & kFacilityTram) ? 128 : 64;
        return (tiles.empty() ? 1 : tiles.size()) * perTile;
    }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(name, owner, town, sign, tiles, facilities, catchment, builtTick, waiting, lastVisitTick, boardedThisMonth,
           boardedLastMonth, rating);
        if (ar.version == 0 || ar.version >= 17) ar(cargoWaiting);
        if (ar.version == 0 || ar.version >= 18) ar(entrances);
    }
};

enum class DepotKind : u8 { Road, Tram, Rail, Ship };

struct Depot {
    CompanyId owner;
    TilePos tile;
    Dir facing = Dir::S; // exit direction
    DepotKind kind = DepotKind::Road;
    TownId town;
    TrackEdgeId edge; // free-form track depots: the track into the building

    template <class Ar>
    void serialize(Ar& ar) {
        ar(owner, tile, facing, kind, town);
        if (ar.version == 0 || ar.version >= 14) ar(edge);
    }
};

namespace stations {

// Nearest town to a tile (within a generous radius), or invalid.
TownId nearestTown(const World& w, TilePos t);
// Creates a station for `owner` at `tile`, naming it after the nearest town.
StationId create(World& w, CompanyId owner, TilePos tile, StopKind kind);
void addTile(World& w, StationId id, TilePos tile, StopKind kind);
// Removes a tile from its station; deletes the station when no tiles remain.
void removeTile(World& w, StationId id, TilePos tile);
// Finds an adjacent station of `owner` that a new stop at `tile` should join.
StationId findJoinable(const World& w, CompanyId owner, TilePos tile);
// Stations whose catchment area covers the given tile.
void stationsCovering(const World& w, TilePos tile, std::vector<StationId>& out);
// Rebuilds the derived catchment index (after loading or station changes).
void rebuildIndex(World& w);
void monthly(World& w);
void removeEntrances(World& w, StationId station);
// A vehicle stopped at the station: records the visit (with a news item for the very first one).
void recordVisit(World& w, StationId id, VehicleId vehicle);

} // namespace stations

} // namespace tp
