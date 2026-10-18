#pragma once
// Names and descriptions of the terrain choices for a new world, and the settings that the
// client's preferences make.

#include "core/preferences.hpp"
#include "sim/terrain_gen.hpp"

#include <optional>
#include <string_view>

namespace tp {

struct LandmassInfo {
    const char* name;
    const char* key; // command line name
    const char* description;
};

const LandmassInfo& landmassInfo(Landmass m);
const LandmassInfo& climateInfo(Climate c);
std::optional<Climate> climateByKey(std::string_view key);
std::optional<Landmass> landmassByKey(std::string_view key);
// "None", "Few", "Normal", "Many", then the extreme for mountains, lakes or rivers.
const char* mountainAmountName(int amount);
const char* lakeAmountName(int amount);
const char* riverAmountName(int amount);

TerrainSettings terrainSettings(const ClientPreferences& p);

} // namespace tp
