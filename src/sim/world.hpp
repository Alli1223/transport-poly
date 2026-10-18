#pragma once
// The complete, deterministic game state plus the per-tick simulation entry point.

#include "core/calendar.hpp"
#include "core/pool.hpp"
#include "core/rng.hpp"
#include "sim/buildings.hpp"
#include "sim/company.hpp"
#include "sim/economy.hpp"
#include "sim/ids.hpp"
#include "sim/map.hpp"
#include "sim/passengers.hpp"
#include "sim/person.hpp"
#include "sim/station.hpp"
#include "sim/town.hpp"
#include "sim/track.hpp"
#include "sim/vehicle.hpp"

#include <deque>
#include <functional>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace tp {

// Save game / snapshot format version.
inline constexpr u32 kSaveVersion = 21;
inline constexpr u32 kMinSaveVersion = 1; // oldest save game that can still be loaded

struct WorldSettings {
    u64 seed = 1;
    i32 startYear = 1950;
    Money initialLoan = 500'000;
    Money maxLoan = 2'000'000;
    i32 interestPercent = 4;
    i32 maxActiveChunks = 20'000;
    i32 maxChunksPerCommand = 64;
    i32 aiCompetitors = 0;
    bool waterways = true;
    bool inflation = false;
    bool recessions = true;
    bool smoothTerrain = true; // smooth hills with varied inclines (worlds before save version 12: stepped)
    TerrainSettings terrain;   // land, mountains, lakes and rivers (worlds before save version 19: the original)
    bool industries = true;    // countryside industries and town needs (worlds before save version 21: off)

    template <class Ar>
    void serialize(Ar& ar) {
        ar(seed, startYear, initialLoan, maxLoan, interestPercent, maxActiveChunks, maxChunksPerCommand);
        if (ar.version == 0 || ar.version >= 17) ar(aiCompetitors);
        if (ar.version == 0 || ar.version >= 5)
            ar(waterways);
        else if constexpr (Ar::kReading)
            waterways = false;
        if (ar.version == 0 || ar.version >= 10) ar(inflation, recessions);
        else if constexpr (Ar::kReading) { inflation = false; recessions = false; }
        if (ar.version == 0 || ar.version >= 12) ar(smoothTerrain);
        else if constexpr (Ar::kReading) smoothTerrain = false;
        if (ar.version == 0 || ar.version >= 19) ar(terrain);
        else if constexpr (Ar::kReading) terrain = TerrainSettings::original();
        if (ar.version == 0 || ar.version >= 21) ar(industries);
        else if constexpr (Ar::kReading) industries = false;
    }
};

enum class NewsKind : u8 { General, Town, Company, Vehicle, Finance };

struct NewsItem {
    i64 tick = 0;
    NewsKind kind = NewsKind::General;
    std::string text;
    std::optional<TilePos> location;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(tick, kind, text, location);
    }
};

class World {
public:
    explicit World(const WorldSettings& settings = {});

    WorldSettings settings;
    GameClock clock;
    Rng rng;
    Map map;
    Pool<Company, CompanyId> companies;
    Pool<Town, TownId> towns;
    Pool<Building, BuildingId> buildings;
    Pool<Person, PersonId> people;
    Pool<Station, StationId> stations;
    Pool<Depot, DepotId> depots;
    Pool<Vehicle, VehicleId> vehicles;
    Pool<TrackNode, TrackNodeId> trackNodes;
    Pool<TrackEdge, TrackEdgeId> trackEdges;
    std::set<u64> processedTownCells; // town cells already founded or known to be empty
    std::set<u64> processedIndustrySites; // industry sites already founded or known to be empty
    u32 nextSerial = 0;               // serial numbers for buildings / people
    std::deque<NewsItem> news;
    Economy economy;
    std::vector<Industry> industries;

    Money inflated(Money base) const { return base / 10'000 * economy.priceIndex + (base % 10'000 * economy.priceIndex + 5000) / 10'000; }
    Money fareIncome(Money base) const { return inflated(base) * (economy.recessionMonths > 0 ? 70 : 100) / 100; }

    // --- Derived state (not saved, rebuilt deterministically) ---------------------------
    // Pending person events ordered by (tick, id).
    std::priority_queue<PersonEvent, std::vector<PersonEvent>, std::greater<PersonEvent>> personEvents;
    // Chunk key -> stations whose catchment touches the chunk.
    std::unordered_map<u64, std::vector<StationId>> stationIndex;
    // Tile keys carrying rail signals or level crossing lights (for rendering).
    std::set<u64> signalTiles;
    // Tile key -> free-form track edges whose bed covers the tile.
    std::unordered_map<u64, std::vector<TrackEdgeId>> trackTiles;
    // Bumped whenever track is built or removed (renderers rebuild their meshes).
    u32 trackRevision = 0;
    // Tile key -> vehicles on the tile (rebuilt every tick).
    std::unordered_map<u64, std::vector<VehicleId>> vehicleTiles;
    RoutingTable routing;

    // Advances the simulation by one tick (15 in-game seconds).
    void tick();

    // Activates a chunk and spawns its content. Returns false if it was already active.
    bool activateChunk(ChunkPos pos);

    void addNews(NewsKind kind, std::string text, std::optional<TilePos> where = std::nullopt);

    // Checksum over the game state, compared between peers to detect desyncs.
    u64 stateHash() const;
    void rebuildSignalIndex();

    template <class Ar>
    void serialize(Ar& ar) {
        ar(settings);
        if constexpr (Ar::kReading) map = Map(settings.seed, settings.waterways, settings.smoothTerrain, settings.terrain);
        ar(clock, rng, map, companies, towns, buildings, people, stations, depots, vehicles, processedTownCells,
           nextSerial, news);
        if (ar.version == 0 || ar.version >= 10) ar(economy);
        if (ar.version == 0 || ar.version >= 13) ar(trackNodes, trackEdges);
        if (ar.version == 0 || ar.version >= 17) ar(industries);
        if (ar.version == 0 || ar.version >= 21) ar(processedIndustrySites);
        if constexpr (Ar::kReading) {
            for (auto [id, c] : companies)
                if (!c.thisMonth.month) {
                    c.thisMonth.year = clock.date().year;
                    c.thisMonth.month = static_cast<u8>(clock.date().month);
                }
            if (ar.version != 0 && ar.version < 17)
                for (auto [id, v] : vehicles) v.loadingStartTick = clock.tick;
            people::rebuildEvents(*this);
            stations::rebuildIndex(*this);
            routing = RoutingTable{};
            rebuildSignalIndex();
            track::rebuildDerived(*this);
            if (ar.version != 0 && ar.version < 18) walking::migrateLegacy(*this);
        }
    }

private:
    void onNewDay();
    void onNewMonth();
    void onNewYear();
};

} // namespace tp
