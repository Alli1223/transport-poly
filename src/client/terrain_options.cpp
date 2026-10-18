#include "client/terrain_options.hpp"

#include <algorithm>

namespace tp {

namespace {

constexpr LandmassInfo kLandmasses[kLandmassCount] = {
    {"Continents", "continents", "Large landmasses with oceans between them"},
    {"Mainland", "mainland", "Land almost everywhere, with inland seas and lakes"},
    {"Islands", "islands", "Islands of every size scattered across open sea"},
    {"Archipelago", "archipelago", "Many small islands with narrow straits between"},
    {"Single island", "island", "One large island around the start, ocean beyond"},
};

constexpr LandmassInfo kClimates[kClimateCount] = {
    {"Temperate", "temperate", "Meadows, woods and snowy peaks"},
    {"Arid", "arid", "Deserts with mesas and dunes, and dry steppe"},
    {"Arctic", "arctic", "Tundra, snowy taiga forests and glaciers"},
    {"Tropical", "tropical", "Jungle, savanna and white beaches"},
    {"Varied", "varied", "Every biome in broad regions: desert, jungle, tundra and more"},
};

} // namespace

const LandmassInfo& climateInfo(Climate c) { return kClimates[std::clamp(static_cast<int>(c), 0, kClimateCount - 1)]; }

std::optional<Climate> climateByKey(std::string_view key) {
    for (int i = 0; i < kClimateCount; ++i)
        if (key == kClimates[i].key) return static_cast<Climate>(i);
    return std::nullopt;
}

const LandmassInfo& landmassInfo(Landmass m) { return kLandmasses[std::clamp(static_cast<int>(m), 0, kLandmassCount - 1)]; }

std::optional<Landmass> landmassByKey(std::string_view key) {
    for (int i = 0; i < kLandmassCount; ++i)
        if (key == kLandmasses[i].key) return static_cast<Landmass>(i);
    return std::nullopt;
}

const char* mountainAmountName(int amount) {
    static constexpr const char* kNames[] = {"None", "Few", "Normal", "Many", "Alpine"};
    return kNames[std::clamp(amount, 0, kTerrainAmountMax)];
}

const char* lakeAmountName(int amount) {
    static constexpr const char* kNames[] = {"None", "Few", "Normal", "Many", "Lots"};
    return kNames[std::clamp(amount, 0, kTerrainAmountMax)];
}

const char* riverAmountName(int amount) {
    static constexpr const char* kNames[] = {"None", "Few", "Normal", "Many"};
    return kNames[std::clamp(amount, 0, kRiverAmountMax)];
}

TerrainSettings terrainSettings(const ClientPreferences& p) {
    TerrainSettings t;
    t.landmass = static_cast<Landmass>(std::clamp(p.landmass, 0, kLandmassCount - 1));
    t.mountains = static_cast<u8>(std::clamp(p.mountains, 0, kTerrainAmountMax));
    t.lakes = static_cast<u8>(std::clamp(p.lakes, 0, kTerrainAmountMax));
    t.rivers = static_cast<u8>(std::clamp(p.rivers, 0, kRiverAmountMax));
    t.climate = static_cast<Climate>(std::clamp(p.climate, 0, kClimateCount - 1));
    return t;
}

} // namespace tp
