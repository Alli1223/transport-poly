#include "render/catenary.hpp"

#include "sim/track.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace tp::gfx {

namespace {

constexpr i32 kSpacing = 360; // track units between masts

glm::vec3 railPoint(const TrackSample& s, const glm::dvec3& origin) {
    return glm::vec3(glm::dvec3(s.p.x / static_cast<double>(kTrackUnits), s.h / static_cast<double>(kSubLevels) * 0.5,
                                s.p.z / static_cast<double>(kTrackUnits)) -
                     origin);
}

// A wire of unit length along +X stretched from a to b.
glm::mat4 span(glm::vec3 a, glm::vec3 b) {
    const glm::vec3 d = b - a;
    const float flat = std::hypot(d.x, d.z);
    glm::mat4 m = glm::translate(glm::mat4(1.0f), a);
    m = glm::rotate(m, std::atan2(-d.z, d.x), glm::vec3(0, 1, 0));
    m = glm::rotate(m, std::atan2(d.y, flat), glm::vec3(0, 0, 1));
    return glm::scale(m, glm::vec3(glm::length(d), 1.0f, 1.0f));
}

} // namespace

void addCatenary(InstanceBatch& batch, const World& w, const glm::dvec3& origin, const glm::dvec3& focus, double radius) {
    for (const auto [id, e] : w.trackEdges) {
        if (!e.electrified || e.samples.size() < 2 || e.length <= 0) continue;
        const TrackSample& mid = e.samples[e.samples.size() / 2];
        const double dx = mid.p.x / static_cast<double>(kTrackUnits) - focus.x, dz = mid.p.z / static_cast<double>(kTrackUnits) - focus.z;
        const double reach = radius * 0.6 + e.length / static_cast<double>(kTrackUnits);
        if (dx * dx + dz * dz > reach * reach) continue;
        // Masts evenly along the edge, the first at its start (the next edge puts one at its end).
        const int n = std::max(1, static_cast<int>(std::lround(static_cast<double>(e.length) / kSpacing)));
        glm::vec3 previousContact{}, previousCatenary{};
        for (int k = 0; k <= n; ++k) {
            const i32 s = static_cast<i32>(static_cast<i64>(e.length) * k / n);
            const TrackSample at = track::pointOn(e, true, s);
            const TrackSample ahead = track::pointOn(e, true, std::min(e.length, s + 32));
            const TrackSample behind = track::pointOn(e, true, std::max(0, s - 32));
            const glm::vec3 rail = railPoint(at, origin);
            const glm::vec3 contact = rail + glm::vec3(0.0f, kContactWireHeight, 0.0f);
            const glm::vec3 catenary = rail + glm::vec3(0.0f, kCatenaryWireHeight, 0.0f);
            if (k > 0) {
                batch.add(models::kCatenaryWire, makeInstance(span(previousContact, contact)));
                batch.add(models::kCatenaryWire, makeInstance(span(previousCatenary, catenary)));
            }
            previousContact = contact;
            previousCatenary = catenary;
            // No mast on the last point (the next edge's first), in tunnels or along platforms.
            if (k == n || e.kind == TrackKind::Platform || track::structureAt(e.spans, s) == TrackStructure::Tunnel) continue;
            const float yaw = static_cast<float>(std::atan2(-static_cast<double>(ahead.p.z - behind.p.z), static_cast<double>(ahead.p.x - behind.p.x)));
            glm::mat4 m = glm::translate(glm::mat4(1.0f), rail);
            m = glm::rotate(m, yaw, glm::vec3(0, 1, 0));
            batch.add(models::kCatenaryMast, makeInstance(m));
        }
    }
}

} // namespace tp::gfx
