#pragma once
#include "sim/track.hpp"
#include <optional>
namespace tp {
struct Person;
struct WalkPoint {
    TPoint p;
    i32 height = -1; // fine height; -1 follows the ground / pavement
    friend bool operator==(const WalkPoint&, const WalkPoint&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(p, height);
    }
};
namespace walking {
std::optional<std::vector<WalkPoint>> route(const World& w, TPoint from, TPoint to);
i64 length(const std::vector<WalkPoint>& path);
WalkPoint position(const Person& person, i64 tick);
bool begin(World& w, PersonId person, std::vector<WalkPoint> path, i64 start);
TPoint endpoint(const World& w, TilePos centre2);
std::optional<std::vector<WalkPoint>> toStation(const World& w, TPoint from, StationId station);
std::optional<std::vector<WalkPoint>> fromStation(const World& w, StationId station, TPoint to);
struct PlatformStairs {
    i32 platform = 0;
    WalkPoint foot, landing;
};
// Shared by pedestrian routing and station geometry, including stations without entrances.
std::vector<PlatformStairs> stationConcourse(const World& w, StationId station);
// Station concourse crosses the tracks on an elevated footbridge, with platform stairs.
std::vector<WalkPoint> withinStation(const World& w, StationId station, TPoint from, TPoint to);
void invalidate(World& w);
void migrateLegacy(World& w);
} // namespace walking
} // namespace tp
