#pragma once

#include <filesystem>

namespace tp {
// Local presentation / control preferences, deliberately outside the simulation state.
struct ClientPreferences {
    bool shadows = true;
    bool sounds = false;
    int aiCompetitors = 0;
    bool vsync = true;
    bool daylightOnly = false;
    int viewDistance = 460;
    int panPercent = 100;
    int orbitPercent = 100;
    int zoomPercent = 100;
    int autosaveMonths = 1; // zero disables automatic saves
    int startYear = 1950;
    int interestPercent = 4;
    bool inflation = false;
    bool recessions = true;
    // New world terrain (see TerrainSettings): landmass index and amounts.
    int landmass = 0;
    int mountains = 2;
    int lakes = 2;
    int rivers = 2;
    int climate = 0;
    void clamp();
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};
}
