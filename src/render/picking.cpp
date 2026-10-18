#include "render/picking.hpp"

#include "render/terrain_mesher.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace tp::gfx {

namespace {

// Möller-Trumbore; returns the ray parameter t of the hit.
std::optional<double> intersect(const Ray& r, const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
    const glm::dvec3 e1 = b - a, e2 = c - a;
    const glm::dvec3 p = glm::cross(r.dir, e2);
    const double det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12) return std::nullopt;
    const double inv = 1.0 / det;
    const glm::dvec3 s = r.origin - a;
    const double u = glm::dot(s, p) * inv;
    if (u < -1e-9 || u > 1.0 + 1e-9) return std::nullopt;
    const glm::dvec3 q = glm::cross(s, e1);
    const double v = glm::dot(r.dir, q) * inv;
    if (v < -1e-9 || u + v > 1.0 + 1e-9) return std::nullopt;
    const double t = glm::dot(e2, q) * inv;
    if (t < 0.0) return std::nullopt;
    return t;
}

std::optional<double> intersectTile(const Map& map, const Ray& r, TilePos t) {
    FineCorners c = map.fineCorners(t);
    if (const Tile* tile = map.tile(t); tile && (tile->span & 3) == kSpanBridge)
        for (u16& height : c.h) height = fineOf(tile->deckHeight);
    const double x0 = t.x, x1 = t.x + 1.0, z0 = t.y, z1 = t.y + 1.0;
    constexpr double k = kHeightStep / kSubLevels;
    const glm::dvec3 nw(x0, c.h[0] * k, z0), ne(x1, c.h[1] * k, z0);
    const glm::dvec3 se(x1, c.h[2] * k, z1), sw(x0, c.h[3] * k, z1);
    std::optional<double> a, b;
    if (!c.splitNESW()) {
        a = intersect(r, nw, se, ne);
        b = intersect(r, nw, sw, se);
    } else {
        a = intersect(r, nw, sw, ne);
        b = intersect(r, ne, sw, se);
    }
    if (a && b) return std::min(*a, *b);
    return a ? a : b;
}

} // namespace

TerrainHit pickTerrain(const Map& map, const Ray& rayIn, int maxTiles) {
    TerrainHit hit;
    Ray ray = rayIn;
    // Skip down to the highest possible terrain height.
    const double top = (kMaxSmoothHeight + 1) * kHeightStep;
    if (ray.origin.y > top) {
        if (ray.dir.y >= 0.0) return hit;
        const double t = (ray.origin.y - top) / -ray.dir.y;
        ray.origin += ray.dir * t;
    }

    i32 tx = static_cast<i32>(std::floor(ray.origin.x));
    i32 tz = static_cast<i32>(std::floor(ray.origin.z));
    const int stepX = ray.dir.x > 0 ? 1 : -1;
    const int stepZ = ray.dir.z > 0 ? 1 : -1;
    const double inf = std::numeric_limits<double>::infinity();
    const double dtX = ray.dir.x != 0.0 ? std::abs(1.0 / ray.dir.x) : inf;
    const double dtZ = ray.dir.z != 0.0 ? std::abs(1.0 / ray.dir.z) : inf;
    double tMaxX = ray.dir.x != 0.0 ? ((stepX > 0 ? (tx + 1.0 - ray.origin.x) : (ray.origin.x - tx)) * dtX) : inf;
    double tMaxZ = ray.dir.z != 0.0 ? ((stepZ > 0 ? (tz + 1.0 - ray.origin.z) : (ray.origin.z - tz)) * dtZ) : inf;

    for (int i = 0; i < maxTiles; ++i) {
        if (const auto t = intersectTile(map, ray, {tx, tz})) {
            hit.hit = true;
            hit.tile = {tx, tz};
            hit.point = ray.origin + ray.dir * *t;
            hit.fx = std::clamp(hit.point.x - tx, 0.0, 0.999999);
            hit.fy = std::clamp(hit.point.z - tz, 0.0, 0.999999);
            hit.corner = {tx + (hit.fx >= 0.5 ? 1 : 0), tz + (hit.fy >= 0.5 ? 1 : 0)};
            const double dn = hit.fy, ds = 1.0 - hit.fy, dw = hit.fx, de = 1.0 - hit.fx;
            hit.edge = Dir::N;
            double best = dn;
            if (ds < best) best = ds, hit.edge = Dir::S;
            if (dw < best) best = dw, hit.edge = Dir::W;
            if (de < best) best = de, hit.edge = Dir::E;
            return hit;
        }
        // Stop once the ray is well below any terrain.
        const double tNow = std::min(tMaxX, tMaxZ);
        if (ray.origin.y + ray.dir.y * tNow < -2.0) break;
        if (tMaxX < tMaxZ) {
            tx += stepX;
            tMaxX += dtX;
        } else {
            tz += stepZ;
            tMaxZ += dtZ;
        }
    }
    return hit;
}

double terrainHeightAt(const Map& map, double x, double z) {
    const i32 tx = static_cast<i32>(std::floor(x));
    const i32 tz = static_cast<i32>(std::floor(z));
    const FineCorners c = map.fineCorners({tx, tz});
    return tileSurfaceLevel(c, static_cast<float>(x - tx), static_cast<float>(z - tz)) * kHeightStep;
}

} // namespace tp::gfx
