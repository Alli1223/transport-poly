#include "sim/station.hpp"

#include "core/math.hpp"
#include "core/names.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <format>

namespace tp::stations {

namespace {

u8 facilityBit(StopKind k) {
    switch (k) {
    case StopKind::Bus: return kFacilityBus;
    case StopKind::Tram: return kFacilityTram;
    case StopKind::Rail: return kFacilityRail;
    case StopKind::Dock: return kFacilityDock;
    }
    return 0;
}

std::string uniqueStationName(const World& w, TownId town, StationId self) {
    const Town* t = w.towns.tryGet(town);
    const std::string base = t ? t->name : std::string("Station");
    for (u32 i = 0; i < 64; ++i) {
        std::string name = base + stationNameSuffix(i);
        bool clash = false;
        for (const auto [id, s] : w.stations)
            if (id != self && s.name == name) clash = true;
        if (!clash) return name;
    }
    return std::format("{} {}", base, self.value);
}

void indexStation(World& w, StationId id) {
    const Station& s = w.stations[id];
    std::vector<u64> keys;
    std::vector<TilePos> indexed = s.tiles;
    for (const auto& entry : s.entrances) indexed.push_back(entry.road);
    for (const TilePos t : indexed) {
        const ChunkPos a = chunkOf({t.x - s.catchment, t.y - s.catchment});
        const ChunkPos b = chunkOf({t.x + s.catchment, t.y + s.catchment});
        for (i32 y = a.y; y <= b.y; ++y)
            for (i32 x = a.x; x <= b.x; ++x) keys.push_back(ChunkPos{x, y}.key());
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    for (const u64 k : keys) w.stationIndex[k].push_back(id);
}

} // namespace

TownId nearestTown(const World& w, TilePos tile) {
    TownId best;
    i64 bestD = 0;
    for (const auto [id, t] : w.towns) {
        const i64 d = chebyshev(t.centre.x - tile.x, t.centre.y - tile.y);
        if (d > 200) continue;
        if (!best.valid() || d < bestD) {
            best = id;
            bestD = d;
        }
    }
    return best;
}

void recordVisit(World& w, StationId id, VehicleId vehicle) {
    Station* s = w.stations.tryGet(id);
    const Vehicle* v = w.vehicles.tryGet(vehicle);
    if (!s || !v) return;
    if (s->lastVisitTick < 0) {
        std::string what(vehicleTypeName(v->info().type));
        for (char& ch : what) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        w.addNews(NewsKind::Company, std::format("Citizens celebrate! First {} arrives at {}", what, s->name), s->sign);
    }
    s->lastVisitTick = w.clock.tick;
}

StationId create(World& w, CompanyId owner, TilePos tile, StopKind kind) {
    const StationId id = w.stations.create();
    Station& s = w.stations[id];
    s.owner = owner;
    s.town = nearestTown(w, tile);
    s.sign = tile;
    s.builtTick = w.clock.tick;
    s.name = uniqueStationName(w, s.town, id);
    addTile(w, id, tile, kind);
    return id;
}

void addTile(World& w, StationId id, TilePos tile, StopKind kind) {
    Station& s = w.stations[id];
    if (std::ranges::find(s.tiles,tile)==s.tiles.end()) s.tiles.push_back(tile);
    s.facilities |= facilityBit(kind);
    s.catchment = std::max(s.catchment, catchmentRadius(kind));
    rebuildIndex(w);
}

void removeTile(World& w, StationId id, TilePos tile) {
    Station& s = w.stations[id];
    std::erase(s.tiles, tile);
    if (s.tiles.empty()) {
        // Waiting passengers are sent home by the people simulation when they notice.
        removeEntrances(w,id);
        w.stations.destroy(id);
    } else if (s.sign == tile) {
        s.sign = s.tiles.front();
    }
    rebuildIndex(w);
}

StationId findJoinable(const World& w, CompanyId owner, TilePos tile) {
    for (i32 dy = -1; dy <= 1; ++dy) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const Tile* t = w.map.tile({tile.x + dx, tile.y + dy});
            if (!t || t->type != TileType::Station) continue;
            const StationId sid(t->index);
            const Station* s = w.stations.tryGet(sid);
            if (s && s->owner == owner) return sid;
        }
    }
    return StationId{};
}

void stationsCovering(const World& w, TilePos tile, std::vector<StationId>& out) {
    out.clear();
    const auto it = w.stationIndex.find(chunkOf(tile).key());
    if (it == w.stationIndex.end()) return;
    for (const StationId id : it->second) {
        const Station* s = w.stations.tryGet(id);
        if (!s) continue;
        std::vector<TilePos> access;
        if ((s->facilities & kFacilityRail) && !s->entrances.empty()) {
            for (const auto& entry : s->entrances) access.push_back(entry.road);
        } else access = s->tiles;
        for (const TilePos t : access) {
            if (chebyshev(t.x - tile.x, t.y - tile.y) <= s->catchment) {
                out.push_back(id);
                break;
            }
        }
    }
}

void removeEntrances(World& w, StationId id) {
    auto* station = w.stations.tryGet(id); if (!station) return;
    for (const auto& entry : station->entrances) for (const auto p : entry.tiles) {
        auto* t = w.map.tile(p);
        if (t && t->type == TileType::Footpath && t->index == id.value) {
            *t = Tile{}; w.map.touch(p);
        }
    }
    station->entrances.clear(); rebuildIndex(w); passengers::markDirty(w);
}

void rebuildIndex(World& w) {
    w.stationIndex.clear();
    for (const auto [id, s] : w.stations) indexStation(w, id);
}

void monthly(World& w) {
    for (auto [id, s] : w.stations) {
        s.boardedLastMonth = s.boardedThisMonth;
        s.boardedThisMonth = 0;
        const i64 days = s.lastVisitTick < 0 ? 30 : std::clamp<i64>((w.clock.tick - s.lastVisitTick) / kTicksPerDay, 0, 30);
        const i32 crowding = static_cast<i32>(s.waitingCount() * 100 / s.waitingCapacity());
        i64 oldestWait = 0;
        for (const auto& [to, queue] : s.waiting)
            for (const PersonId pid : queue)
                if (const Person* p = w.people.tryGet(pid))
                    oldestWait = std::max(oldestWait, w.clock.tick - p->boardTick);
        const i32 waitPenalty = static_cast<i32>(std::clamp<i64>(oldestWait * 80 / (3 * kTicksPerHour), 0, 80));
        const i32 target = std::clamp(180 + std::min(s.boardedLastMonth, 150) / 2 -
                                      static_cast<i32>(days) * 5 - crowding - waitPenalty, 0, 255);
        s.rating = (s.rating * 3 + target) / 4;
    }
}

} // namespace tp::stations
