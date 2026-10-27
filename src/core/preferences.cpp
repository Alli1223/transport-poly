#include "core/preferences.hpp"
#include <algorithm>
#include <fstream>

namespace tp {
void ClientPreferences::clamp() {
    aiCompetitors = std::clamp(aiCompetitors, 0, 3);
    viewDistance = std::clamp(viewDistance, 120, 900);
    panPercent = std::clamp(panPercent, 25, 300);
    orbitPercent = std::clamp(orbitPercent, 25, 300);
    zoomPercent = std::clamp(zoomPercent, 25, 300);
    autosaveMonths = std::clamp(autosaveMonths, 0, 12);
    startYear = std::clamp(startYear, 1900, 2100);
    interestPercent = std::clamp(interestPercent, 0, 20);
    landmass = std::clamp(landmass, 0, 4);
    mountains = std::clamp(mountains, 0, 4);
    lakes = std::clamp(lakes, 0, 4);
    rivers = std::clamp(rivers, 0, 3);
    climate = std::clamp(climate, 0, 4);
    monitor = std::clamp(monitor, 0, 15);
    antiAliasing = std::clamp(antiAliasing, 0, 3);
    frameCap = std::clamp(frameCap, 0, 7);
    shadowQuality = std::clamp(shadowQuality, 0, 3);
    detailQuality = std::clamp(detailQuality, 0, 2);
    treeDistance = std::clamp(treeDistance, 60, 500);
    brightnessPercent = std::clamp(brightnessPercent, 50, 200);
    fogPercent = std::clamp(fogPercent, 0, 150);
    nightFogPercent = std::clamp(nightFogPercent, 0, 100);
    masterVolume = std::clamp(masterVolume, 0, 100);
    musicVolume = std::clamp(musicVolume, 0, 100);
    vehicleVolume = std::clamp(vehicleVolume, 0, 100);
    ambienceVolume = std::clamp(ambienceVolume, 0, 100);
    interfaceVolume = std::clamp(interfaceVolume, 0, 100);
}
int ClientPreferences::frameRateLimit() const {
    constexpr int caps[] = {0, 30, 60, 90, 120, 144, 165, 240};
    return caps[std::clamp(frameCap, 0, 7)];
}
void ClientPreferences::graphicsPreset(int quality) {
    quality = std::clamp(quality, 0, 2);
    shadows = quality > 0;
    shadowQuality = quality == 2 ? 3 : quality == 1 ? 2 : 0;
    antiAliasing = quality == 2 ? 3 : quality == 1 ? 2 : 0;
    detailQuality = quality;
    viewDistance = quality == 2 ? 700 : quality == 1 ? 460 : 300;
    treeDistance = quality == 2 ? 400 : quality == 1 ? 200 : 120;
    clouds = cloudShadows = particles = treeSway = waterAnimation = quality > 0;
    fogPercent = quality == 0 ? 50 : 100;
    lightHalos = true;
}
bool ClientPreferences::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    ClientPreferences candidate;
    int version = 0;
    if (!(in >> version) || (version < 1 || version > 10) ||
        !(in >> candidate.shadows >> candidate.vsync >> candidate.daylightOnly >> candidate.viewDistance >>
          candidate.panPercent >> candidate.orbitPercent >> candidate.zoomPercent >> candidate.autosaveMonths >>
          candidate.startYear >> candidate.interestPercent)) return false;
    if (version >= 2 && !(in >> candidate.inflation >> candidate.recessions)) return false;
    if (version >= 3 && !(in >> candidate.sounds >> candidate.aiCompetitors)) return false;
    if (version >= 4 && !(in >> candidate.landmass >> candidate.mountains >> candidate.lakes >> candidate.rivers)) return false;
    if (version >= 5 && !(in >> candidate.climate)) return false;
    if (version >= 6 && !(in >> candidate.weather)) return false;
    if (version >= 7) {
        std::string skipped;
        if (!(in >> candidate.checkUpdates >> skipped)) return false;
        candidate.skippedVersion = skipped == "-" ? std::string() : skipped;
    }
    if (version >= 8 && !(in >> candidate.fullscreen >> candidate.monitor >> candidate.antiAliasing >> candidate.frameCap >>
          candidate.shadowQuality >> candidate.detailQuality >> candidate.treeDistance >> candidate.brightnessPercent >>
          candidate.clouds >> candidate.cloudShadows >> candidate.fogPercent >> candidate.nightFogPercent >> candidate.particles >>
          candidate.lightHalos >> candidate.treeSway >> candidate.waterAnimation)) return false;
    if (version >= 9 && !(in >> candidate.masterVolume >> candidate.musicVolume >> candidate.vehicleVolume >>
          candidate.ambienceVolume >> candidate.interfaceVolume >> candidate.muteUnfocused)) return false;
    if (version >= 10 && !(in >> candidate.trackContours)) return false;
    candidate.clamp();
    *this = candidate;
    return true;
}
bool ClientPreferences::save(const std::filesystem::path& path) const {
    ClientPreferences p = *this;
    p.clamp();
    std::ofstream out(path, std::ios::trunc);
    out << 10 << '\n' << p.shadows << ' ' << p.vsync << ' ' << p.daylightOnly << ' ' << p.viewDistance << ' '
        << p.panPercent << ' ' << p.orbitPercent << ' ' << p.zoomPercent << ' ' << p.autosaveMonths << ' '
        << p.startYear << ' ' << p.interestPercent << ' ' << p.inflation << ' ' << p.recessions << ' ' << p.sounds << ' ' << p.aiCompetitors << ' '
        << p.landmass << ' ' << p.mountains << ' ' << p.lakes << ' ' << p.rivers << ' ' << p.climate << ' ' << p.weather << ' ' << p.checkUpdates << ' '
        << (p.skippedVersion.empty() ? std::string("-") : p.skippedVersion) << '\n'
        << p.fullscreen << ' ' << p.monitor << ' ' << p.antiAliasing << ' ' << p.frameCap << ' ' << p.shadowQuality << ' '
        << p.detailQuality << ' ' << p.treeDistance << ' ' << p.brightnessPercent << ' ' << p.clouds << ' ' << p.cloudShadows << ' '
        << p.fogPercent << ' ' << p.nightFogPercent << ' ' << p.particles << ' ' << p.lightHalos << ' ' << p.treeSway << ' '
        << p.waterAnimation << '\n'
        << p.masterVolume << ' ' << p.musicVolume << ' ' << p.vehicleVolume << ' ' << p.ambienceVolume << ' '
        << p.interfaceVolume << ' ' << p.muteUnfocused << '\n'
        << p.trackContours << '\n';
    out.flush();
    return static_cast<bool>(out);
}
}
