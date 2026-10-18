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
}
bool ClientPreferences::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    ClientPreferences candidate;
    int version = 0;
    if (!(in >> version) || (version < 1 || version > 5) ||
        !(in >> candidate.shadows >> candidate.vsync >> candidate.daylightOnly >> candidate.viewDistance >>
          candidate.panPercent >> candidate.orbitPercent >> candidate.zoomPercent >> candidate.autosaveMonths >>
          candidate.startYear >> candidate.interestPercent)) return false;
    if (version >= 2 && !(in >> candidate.inflation >> candidate.recessions)) return false;
    if (version >= 3 && !(in >> candidate.sounds >> candidate.aiCompetitors)) return false;
    if (version >= 4 && !(in >> candidate.landmass >> candidate.mountains >> candidate.lakes >> candidate.rivers)) return false;
    if (version >= 5 && !(in >> candidate.climate)) return false;
    candidate.clamp();
    *this = candidate;
    return true;
}
bool ClientPreferences::save(const std::filesystem::path& path) const {
    ClientPreferences p = *this;
    p.clamp();
    std::ofstream out(path, std::ios::trunc);
    out << 5 << '\n' << p.shadows << ' ' << p.vsync << ' ' << p.daylightOnly << ' ' << p.viewDistance << ' '
        << p.panPercent << ' ' << p.orbitPercent << ' ' << p.zoomPercent << ' ' << p.autosaveMonths << ' '
        << p.startYear << ' ' << p.interestPercent << ' ' << p.inflation << ' ' << p.recessions << ' ' << p.sounds << ' ' << p.aiCompetitors << ' '
        << p.landmass << ' ' << p.mountains << ' ' << p.lakes << ' ' << p.rivers << ' ' << p.climate << '\n';
    out.flush();
    return static_cast<bool>(out);
}
}
