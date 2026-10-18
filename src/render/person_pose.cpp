#include "render/person_pose.hpp"
#include "render/picking.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <cmath>

namespace tp::gfx {
std::optional<PersonPose> personPose(const World& w, const Person& p, double fraction) {
    const auto buildingPos = [&](BuildingRef ref) -> std::optional<glm::dvec2> {
        const Building* b = w.buildings.tryGet(ref.id);
        if (!b || b->serial != ref.serial) return std::nullopt;
        const TilePos centre = b->centre2();
        return glm::dvec2(centre.x * 0.5, centre.y * 0.5);
    };
    if (p.state == PersonState::Riding) {
        if (const Vehicle* v = w.vehicles.tryGet(p.vehicle))
            if (const auto pose = vehiclePose(w, *v, fraction)) return PersonPose{pose->pos, pose->yaw, false};
    }
    glm::dvec2 from(p.tripFrom2.x * 0.5, p.tripFrom2.y * 0.5);
    glm::dvec2 to(p.tripTo2.x * 0.5, p.tripTo2.y * 0.5);
    bool visible = false;
    bool moving = false;
    i64 speed = kWalkTicksPerTile, overhead = 0;
    if (p.state == PersonState::AtHome || p.state == PersonState::AtWork || p.state == PersonState::AtShop) {
        const auto at = buildingPos(p.state == PersonState::AtHome ? p.home : p.state == PersonState::AtWork ? p.work : p.shop);
        if (!at) return std::nullopt;
        from = to = *at;
    } else if (p.state == PersonState::Waiting || p.state == PersonState::ToStop || p.state == PersonState::FromStop) {
        if (const Station* s = w.stations.tryGet(p.station)) {
            const glm::dvec2 stop(s->sign.x + 0.5, s->sign.y + 0.5);
            if (p.state == PersonState::Waiting) {
                const double ox = (static_cast<int>(p.serial % 7) - 3) * 0.045;
                const double oy = (static_cast<int>((p.serial / 7) % 7) - 3) * 0.045;
                from = to = stop + glm::dvec2(ox, oy);
            } else if (p.state == PersonState::ToStop) to = stop;
            else from = stop;
        }
        visible = true;
        moving = p.state != PersonState::Waiting;
    } else if (p.state == PersonState::Walking || p.state == PersonState::Driving || p.state == PersonState::Lift) {
        moving = true;
        visible = p.state == PersonState::Walking;
        if (p.state == PersonState::Driving) { speed = kDriveTicksPerTile; overhead = kDriveOverheadTicks; }
        if (p.state == PersonState::Lift) { speed = kLiftTicksPerTile; overhead = kLiftOverheadTicks; }
    }
    double along = 0;
    if (moving && p.nextEvent >= 0) {
        const double tiles = std::ceil(std::abs(to.x - from.x) + std::abs(to.y - from.y));
        const double duration = std::max(1.0, tiles) * static_cast<double>(speed) + static_cast<double>(overhead);
        along = std::clamp(1.0 - (static_cast<double>(p.nextEvent - w.clock.tick) - fraction) / duration, 0.0, 1.0);
    }
    const auto at = glm::mix(from, to, along);
    return PersonPose{{at.x, terrainHeightAt(w.map, at.x, at.y) + 0.035, at.y},
                      static_cast<float>(std::atan2(-(to.y - from.y), to.x - from.x)), visible};
}
}
