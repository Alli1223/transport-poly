#include "render/vehicle_pose.hpp"

#include "render/terrain_mesher.hpp"
#include "sim/rail.hpp"
#include "sim/road.hpp"
#include "sim/water_path.hpp"

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
    if (!t || t->type == TileType::Clear) return tileSurfaceLevel(w.map.fineCorners(tile), u, v) * kHeightStep;
    if (t->span) return t->deckHeight * kHeightStep;
    u8 pieces = allRoadPieces(*t);
    if (t->type == TileType::Depot) pieces = 0;
    Surface surf = t->type == TileType::Road || t->type == TileType::Station ? roadSurface(s, pieces)
                                                                             : buildingSurface(s);
    if (t->type == TileType::Rail) surf = railSurface(s, t->rail);
    if (surf == Surface::Ramp) return tileSurfaceLevel(c, u, v) * kHeightStep;
    return static_cast<float>(flatSurfaceLevel(s, surf == Surface::Invalid ? Surface::Foundation : surf)) *
           kHeightStep;
}

namespace {

glm::vec2 edgeMid(Dir d) {
    switch (d) {
    case Dir::N: return {0.5f, 0.0f};
    case Dir::E: return {1.0f, 0.5f};
    case Dir::S: return {0.5f, 1.0f};
    case Dir::W: return {0.0f, 0.5f};
    }
    return {0.5f, 0.5f};
}

i32 segLength(const TrackSeg& s) { return trackLength(trackBetween(s.entry, s.exit)); }

// World position of the point `along` units from the entry of a track segment.
glm::dvec3 pointOnSeg(const World& w, const TrackSeg& s, double along) {
    const double len = segLength(s);
    const float t = static_cast<float>(std::clamp(along / len, 0.0, 1.0));
    const glm::vec2 p = glm::mix(edgeMid(s.entry), edgeMid(s.exit), t);
    const float y = roadSurfaceY(w, s.tile, std::clamp(p.x, 0.0f, 1.0f), std::clamp(p.y, 0.0f, 1.0f));
    return {s.tile.x + p.x, y + 0.03, s.tile.y + p.y};
}

// Point `back` units behind the (extrapolated) head; nullopt beyond the end of the trail.
std::optional<glm::dvec3> trainPoint(const World& w, const Vehicle& v, double headProgress, double back) {
    double s = headProgress - back;
    if (s >= 0.0) {
        const double l0 = segLength(v.trail.front());
        if (s <= l0 || v.ahead.empty()) return pointOnSeg(w, v.trail.front(), std::min(s, l0));
        return pointOnSeg(w, v.ahead.front(), s - l0);
    }
    double remaining = -s;
    for (usize i = 1; i < v.trail.size(); ++i) {
        const double l = segLength(v.trail[i]);
        if (remaining <= l) return pointOnSeg(w, v.trail[i], l - remaining);
        remaining -= l;
    }
    return std::nullopt;
}

} // namespace

void trainCarPoses(const World& w, const Vehicle& v, double tickFraction, std::vector<CarPose>& out) {
    if (v.state == VehicleState::InDepot || v.trail.empty()) return;
    double head = v.progress;
    if (v.state == VehicleState::Driving && !v.stopped) head += v.speed * tickFraction;
    double offset = 0.0;
    const std::vector<u16> single{v.model};
    const std::vector<u16>& cars = v.cars.empty() ? single : v.cars;
    for (const u16 m : cars) {
        const double len = vehicleModel(m).length * 16.0;
        const double centre = offset + len * 0.5;
        const auto front = trainPoint(w, v, head, centre - len * 0.4);
        const auto rear = trainPoint(w, v, head, centre + len * 0.4);
        offset += len + 4.0;
        if (!front || !rear) continue;
        const glm::dvec3 d = *front - *rear;
        CarPose cp;
        cp.model = m;
        cp.pose.pos = (*front + *rear) * 0.5;
        cp.pose.yaw = static_cast<float>(std::atan2(-d.z, d.x));
        cp.pose.pitch = static_cast<float>(std::atan2(d.y, std::max(1e-6, std::sqrt(d.x * d.x + d.z * d.z))));
        out.push_back(cp);
    }
}

namespace {

float headingYaw(u8 h) {
    return static_cast<float>(std::atan2(-static_cast<double>(kHeadingDY[h & 7]), static_cast<double>(kHeadingDX[h & 7])));
}

float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a, glm::two_pi<float>());
    if (d > glm::pi<float>()) d -= glm::two_pi<float>();
    if (d < -glm::pi<float>()) d += glm::two_pi<float>();
    return a + d * t;
}

// Ships glide between tile centres, turning smoothly through each centre.
VehiclePose shipPose(const Vehicle& v, double tickFraction) {
    VehiclePose pose;
    const double len = headingLength(v.heading);
    double prog = v.progress;
    if (v.moving && v.state == VehicleState::Driving && !v.stopped) prog += v.speed * tickFraction;
    const double f = v.moving ? std::clamp(prog / len, 0.0, 1.0) : 0.0;
    pose.pos = {v.tile.x + 0.5 + kHeadingDX[v.heading & 7] * f, kWaterLevel, v.tile.y + 0.5 + kHeadingDY[v.heading & 7] * f};
    const float cur = headingYaw(v.heading);
    if (!v.moving) {
        pose.yaw = cur;
        return pose;
    }
    const float prev = headingYaw(v.prevHeading);
    const float next = v.pathIndex < v.path.size() ? headingYaw(v.path[v.pathIndex]) : cur;
    const float ff = static_cast<float>(f);
    pose.yaw = ff < 0.5f ? lerpAngle(lerpAngle(prev, cur, 0.5f), cur, ff * 2.0f)
                         : lerpAngle(cur, lerpAngle(cur, next, 0.5f), (ff - 0.5f) * 2.0f);
    return pose;
}

} // namespace

std::optional<VehiclePose> vehiclePose(const World& w, const Vehicle& v, double tickFraction) {
    if (v.info().type == VehicleType::Ship) {
        if (v.state == VehicleState::InDepot) return std::nullopt;
        return shipPose(v, tickFraction);
    }
    if (v.info().type == VehicleType::Rail) {
        std::vector<CarPose> poses;
        trainCarPoses(w, v, tickFraction, poses);
        if (poses.empty()) return std::nullopt;
        return poses.front().pose;
    }
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
