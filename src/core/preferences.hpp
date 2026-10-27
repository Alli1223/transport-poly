#pragma once

#include <filesystem>
#include <string>

namespace tp {
// Local presentation / control preferences, deliberately outside the simulation state.
struct ClientPreferences {
    bool shadows = true;
    bool sounds = true;
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
    bool weather = true; // clouds, cloud shadows and mist
    bool checkUpdates = true;   // look for a newer release when the game starts
    std::string skippedVersion; // a release the player chose not to be offered again
    bool fullscreen = true;
    int monitor = 0;
    int antiAliasing = 2; // off, 2x, 4x, 8x MSAA; applied on startup
    int frameCap = 0; // unlimited, 30, 60, 90, 120, 144, 165, 240 FPS
    int shadowQuality = 2; // 512, 1024, 2048, 4096 pixels
    int detailQuality = 1; // low, balanced, high terrain detail
    int treeDistance = 200;
    int brightnessPercent = 100;
    bool clouds = true;
    bool cloudShadows = true;
    int fogPercent = 100;
    int nightFogPercent = 25;
    bool particles = true;
    bool lightHalos = true;
    bool treeSway = true;
    bool waterAnimation = true;
    int masterVolume = 75, musicVolume = 55, vehicleVolume = 80, ambienceVolume = 60, interfaceVolume = 75;
    bool muteUnfocused = true;
    bool trackContours = true; // the elevation view while laying track (H)
    int frameRateLimit() const;
    void graphicsPreset(int quality);
    void clamp();
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};
}
