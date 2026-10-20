#include "client/map_draw.hpp"

#include "render/picking.hpp"

#include <algorithm>
#include <cmath>

namespace tp::mapdraw {

glm::dvec3 groundPoint(const World& w, double x, double z, double lift) {
    return {x, gfx::terrainHeightAt(w.map, x, z) + lift, z};
}

void addRing(std::vector<gfx::OverlayQuad>& overlays, glm::dvec3 centre, double radius, u32 colour) {
    constexpr int kSides = 12;
    const double inner = radius * 0.72;
    for (int i = 0; i < kSides; ++i) {
        const double a0 = i * 6.283185307 / kSides, a1 = (i + 1) * 6.283185307 / kSides;
        const glm::dvec3 d0(std::cos(a0), 0.0, std::sin(a0)), d1(std::cos(a1), 0.0, std::sin(a1));
        overlays.push_back({{centre + d0 * inner, centre + d0 * radius, centre + d1 * radius, centre + d1 * inner}, colour, true});
    }
}

void addRibbon(std::vector<gfx::OverlayQuad>& overlays, const std::vector<glm::dvec3>& points, double halfWidth, u32 colour,
               double dash, double offset) {
    // Sideways shift of each point, along the average of the neighbouring segments' normals.
    const auto normalAt = [&](usize i) {
        glm::dvec2 n(0.0);
        for (const usize j : {i, i + 1}) {
            if (j == 0 || j >= points.size()) continue;
            const glm::dvec2 d(points[j].x - points[j - 1].x, points[j].z - points[j - 1].z);
            const double len = glm::length(d);
            if (len > 1e-6) n += glm::dvec2(-d.y, d.x) / len;
        }
        const double len = glm::length(n);
        return len > 1e-6 ? n / len : n;
    };
    std::vector<glm::dvec3> shifted = points;
    if (offset != 0.0)
        for (usize i = 0; i < points.size(); ++i) {
            const glm::dvec2 n = normalAt(i) * offset;
            shifted[i] += glm::dvec3(n.x, 0.0, n.y);
        }
    double run = 0.0;
    for (usize i = 1; i < shifted.size(); ++i) {
        const glm::dvec3 a = shifted[i - 1], b = shifted[i];
        const glm::dvec3 d = b - a;
        const double len = glm::length(glm::dvec2(d.x, d.z));
        if (len < 1e-4) continue;
        const glm::dvec3 side(-d.z / len * halfWidth, 0.0, d.x / len * halfWidth);
        if (dash <= 0.0) {
            overlays.push_back({{a - side, a + side, b + side, b - side}, colour, true});
            continue;
        }
        // Dashes continue across corners.
        double at = 0.0;
        while (at < len) {
            const double phase = std::fmod(run + at, dash * 2.0);
            const double step = std::min(len - at, (phase < dash ? dash : dash * 2.0) - phase);
            if (phase < dash) {
                const glm::dvec3 p = a + d * (at / len), q = a + d * ((at + step) / len);
                overlays.push_back({{p - side, p + side, q + side, q - side}, colour, true});
            }
            at += step + 1e-6;
        }
        run += len;
    }
}

std::vector<glm::dvec3> groundLeg(const World& w, glm::dvec3 a, glm::dvec3 b, double lift) {
    std::vector<glm::dvec3> out;
    const double len = glm::length(glm::dvec2(b.x - a.x, b.z - a.z));
    const int pieces = std::clamp(static_cast<int>(len / 0.75), 1, 256);
    for (int i = 0; i <= pieces; ++i) {
        const double t = static_cast<double>(i) / pieces;
        out.push_back(groundPoint(w, a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, lift));
    }
    return out;
}

} // namespace tp::mapdraw
