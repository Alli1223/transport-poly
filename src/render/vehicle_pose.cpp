#include "render/vehicle_pose.hpp"

#include "render/terrain_mesher.hpp"
#include "sim/road.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace tp::gfx {

namespace {

// Position and direction in the canonical frame (travelling north, entering from the south edge).
struct LocalPoint {
    glm::vec2 p;
    glm::vec2 dir;
};

LocalPoint canonical(TurnKind kind, float t) {
    constexpr float l = kLaneOffset;
    const float halfPi = glm::half_pi<float>();
    switch (kind) {
    case TurnKind::Straight: return {{0.5f + l, 1.0f - t}, {0.0f, -1.0f}};
    case TurnKind::Right: {
        // Quarter circle around the SE corner.
        const float r = 0.5f - l;
        const float phi = glm::pi<float>() + halfPi * t;
        return {{1.0f + r * std::cos(phi), 1.0f + r * std::sin(phi)}, {-std::sin(phi), std::cos(phi)}};
    }
    case TurnKind::Left: {
        // Quarter circle around the SW corner.
        const float r = 0.5f + l;
        const float phi = -halfPi * t;
        return {{r * std::cos(phi), 1.0f + r * std::sin(phi)}, {std::sin(phi), -std::cos(phi)}};
    }
    case TurnKind::UTurn: {
        // In, half circle around (0.5, 0.65), back out on the other lane.
        const float straight = 0.35f;
        const float arc = glm::pi<float>() * l;
        const float total = straight * 2.0f + arc;
        const float s = t * total;
        if (s < straight) return {{0.5f + l, 1.0f - s}, {0.0f, -1.0f}};
        if (s < straight + arc) {
            const float phi = -(s - straight) / l; // 0 .. -pi
            return {{0.5f + l * std::cos(phi), 0.65f + l * std::sin(phi)}, {std::sin(phi), -std::cos(phi)}};
        }
        const float back = s - straight - arc;
        return {{0.5f - l, 0.65f + back}, {0.0f, 1.0f}};
    }
    }
    return {{0.5f, 0.5f}, {0.0f, -1.0f}};
}

// Rotates a canonical (travelling north) point to the given travel direction.
glm::vec2 rotatePoint(glm::vec2 p, Dir travel) {
    switch (travel) {
    case Dir::N: return p;
    case Dir::E: return {1.0f - p.y, p.x};
    case Dir::S: return {1.0f - p.x, 1.0f - p.y};
    case Dir::W: return {p.y, 1.0f - p.x};
    }
    return p;
}

glm::vec2 rotateDir(glm::vec2 d, Dir travel) {
    switch (travel) {
    case Dir::N: return d;
    case Dir::E: return {-d.y, d.x};
    case Dir::S: return {-d.x, -d.y};
    case Dir::W: return {d.y, -d.x};
    }
    return d;
}

} // namespace

float roadSurfaceY(const World& w, TilePos tile, float u, float v) {
    const Tile* t = w.map.tile(tile);
    const TileCorners c = w.map.corners(tile);
    const Slope s = computeSlope(c);
    if (!t) return tileSurfaceLevel(c, u, v) * kHeightStep;
    u8 pieces = allRoadPieces(*t);
    if (t->type == TileType::Depot) pieces = 0;
    const Surface surf = t->type == TileType::Road || t->type == TileType::Station ? roadSurface(s, pieces)
                                                                                   : buildingSurface(s);
    if (surf == Surface::Ramp) return tileSurfaceLevel(c, u, v) * kHeightStep;
    return static_cast<float>(flatSurfaceLevel(s, surf == Surface::Invalid ? Surface::Foundation : surf)) *
           kHeightStep;
}

std::optional<VehiclePose> vehiclePose(const World& w, const Vehicle& v, double tickFraction) {
    if (v.state == VehicleState::InDepot) return std::nullopt;
    const i32 len = pathLength(v.entry, v.exit);
    double progress = v.progress;
    if (v.state == VehicleState::Driving && !v.stopped) progress += v.speed * tickFraction;
    const float t = static_cast<float>(std::clamp(progress / len, 0.0, 1.0));
    const Dir travel = opposite(v.entry);
    const LocalPoint lp = canonical(turnKind(v.entry, v.exit), t);
    const glm::vec2 p = rotatePoint(lp.p, travel);
    const glm::vec2 d = rotateDir(lp.dir, travel);
    VehiclePose pose;
    const float y = roadSurfaceY(w, v.tile, std::clamp(p.x, 0.0f, 1.0f), std::clamp(p.y, 0.0f, 1.0f));
    pose.pos = glm::dvec3(v.tile.x + p.x, y + 0.03, v.tile.y + p.y);
    pose.yaw = std::atan2(-d.y, d.x);
    // Pitch from the surface slope along the heading.
    const float ahead = roadSurfaceY(w, v.tile, std::clamp(p.x + d.x * 0.15f, 0.0f, 1.0f),
                                     std::clamp(p.y + d.y * 0.15f, 0.0f, 1.0f));
    const float behind = roadSurfaceY(w, v.tile, std::clamp(p.x - d.x * 0.15f, 0.0f, 1.0f),
                                      std::clamp(p.y - d.y * 0.15f, 0.0f, 1.0f));
    pose.pitch = std::atan2(ahead - behind, 0.3f);
    return pose;
}

} // namespace tp::gfx
