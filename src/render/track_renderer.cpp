#include "render/track_renderer.hpp"

#include "render/terrain_mesher.hpp"

#include <algorithm>
#include <cmath>

namespace tp::gfx {

namespace {

constexpr u32 kBallast = rgba(128, 118, 106);
constexpr u32 kBallastSide = rgba(112, 104, 94);
constexpr u32 kSleeper = rgba(96, 74, 56);
constexpr u32 kSteel = rgba(150, 152, 158);
constexpr u32 kPlatform = rgba(188, 182, 170);
constexpr u32 kPlatformSide = rgba(150, 144, 134);
constexpr u32 kPlatformEdge = rgba(232, 206, 90);
constexpr u32 kCanopy = rgba(150, 156, 166);
constexpr u32 kPost = rgba(96, 100, 108);

constexpr float kBallastTop = -0.035f; // relative to the nominal rail height
constexpr float kSleeperTop = -0.027f;
constexpr float kRailTop = -0.017f;
constexpr float kBallastHalf = 0.27f;
constexpr float kBallastFoot = 0.42f;
constexpr float kGauge = 0.07f;
constexpr float kRailHalf = 0.012f;

struct Frame {
    glm::vec3 p;     // centre line position (rail height)
    glm::vec2 left;  // unit vector to the left of the direction of travel
};

} // namespace

float trackRailY(i32 fineHeight) { return static_cast<float>(fineHeight) * (kHeightStep / kSubLevels) + kRailTop; }

void buildTrackMesh(const TrackEdge& edge, TilePos anchor, MeshBuilder& out) {
    const auto& sm = edge.samples;
    if (sm.size() < 2) return;
    constexpr float kUnit = 1.0f / kTrackUnits;
    const float k = kHeightStep / kSubLevels;
    std::vector<Frame> frames(sm.size());
    for (usize i = 0; i < sm.size(); ++i) {
        const TrackSample& a = sm[i == 0 ? 0 : i - 1];
        const TrackSample& b = sm[std::min(i + 1, sm.size() - 1)];
        glm::vec2 t(static_cast<float>(b.p.x - a.p.x), static_cast<float>(b.p.z - a.p.z));
        t = glm::length(t) > 0.0f ? glm::normalize(t) : glm::vec2(1, 0);
        frames[i].p = glm::vec3(sm[i].p.x * kUnit - anchor.x, sm[i].h * k, sm[i].p.z * kUnit - anchor.y);
        frames[i].left = glm::vec2(t.y, -t.x);
    }
    const auto at = [](const Frame& f, float side, float dy) {
        return glm::vec3(f.p.x + f.left.x * side, f.p.y + dy, f.p.z + f.left.y * side);
    };
    // Ballast: a flat top with sloping shoulders.
    for (usize i = 0; i + 1 < frames.size(); ++i) {
        const Frame& a = frames[i];
        const Frame& b = frames[i + 1];
        out.triangleUp(at(a, -kBallastHalf, kBallastTop), at(b, kBallastHalf, kBallastTop), at(b, -kBallastHalf, kBallastTop),
                       kBallast);
        out.triangleUp(at(a, -kBallastHalf, kBallastTop), at(a, kBallastHalf, kBallastTop), at(b, kBallastHalf, kBallastTop),
                       kBallast);
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec3 a0 = at(a, side * kBallastHalf, kBallastTop), b0 = at(b, side * kBallastHalf, kBallastTop);
            const glm::vec3 a1 = at(a, side * kBallastFoot, kBallastTop - 0.17f), b1 = at(b, side * kBallastFoot, kBallastTop - 0.17f);
            out.triangleUp(a0, b0, b1, kBallastSide);
            out.triangleUp(a0, b1, a1, kBallastSide);
        }
        // Rails.
        for (const float rail : {-kGauge, kGauge}) {
            const glm::vec3 a0 = at(a, rail - kRailHalf, kRailTop), a1 = at(a, rail + kRailHalf, kRailTop);
            const glm::vec3 b0 = at(b, rail - kRailHalf, kRailTop), b1 = at(b, rail + kRailHalf, kRailTop);
            out.triangleUp(a0, b1, b0, kSteel);
            out.triangleUp(a0, a1, b1, kSteel);
        }
    }
    if (edge.kind == TrackKind::Platform) {
        // Platforms either side of the track, with an edge line and a canopy on posts.
        constexpr float kInner = 0.33f, kOuter = 0.93f, kTop = 0.05f, kFoot = -0.16f;
        constexpr float kRoofIn = 0.42f, kRoofOut = 0.88f, kRoof = 0.46f;
        for (usize i = 0; i + 1 < frames.size(); ++i) {
            const Frame& a = frames[i];
            const Frame& b = frames[i + 1];
            for (const float side : {-1.0f, 1.0f}) {
                const auto quadTop = [&](float in, float outer, float dy, u32 c) {
                    out.triangleUp(at(a, side * in, dy), at(b, side * outer, dy), at(b, side * in, dy), c);
                    out.triangleUp(at(a, side * in, dy), at(a, side * outer, dy), at(b, side * outer, dy), c);
                };
                quadTop(kInner, kOuter, kTop, kPlatform);
                quadTop(kInner, kInner + 0.035f, kTop + 0.002f, kPlatformEdge);
                quadTop(kRoofIn, kRoofOut, kRoof, kCanopy);
                // Platform walls facing the track and the outside.
                const glm::vec3 fwd(b.p.x - a.p.x, 0.0f, b.p.z - a.p.z);
                for (const float edgeOff : {kInner, kOuter}) {
                    const glm::vec3 a0 = at(a, side * edgeOff, kTop), b0 = at(b, side * edgeOff, kTop);
                    const glm::vec3 a1 = at(a, side * edgeOff, kFoot), b1 = at(b, side * edgeOff, kFoot);
                    const float facing = edgeOff == kInner ? -side : side;
                    out.quadFacing(a1, b1, b0, a0, kPlatformSide,
                                   glm::vec3(a.left.x * facing, 0.0f, a.left.y * facing));
                    (void)fwd;
                }
            }
        }
        // Canopy posts every couple of tiles.
        const float length = static_cast<float>(edge.length) / kTrackUnits;
        const int posts = std::max(2, static_cast<int>(length / 1.6f) + 1);
        for (int n = 0; n < posts; ++n) {
            const usize idx = std::min(frames.size() - 1, static_cast<usize>((frames.size() - 1) * n / std::max(1, posts - 1)));
            const Frame& f = frames[idx];
            for (const float side : {-1.0f, 1.0f}) {
                const glm::vec3 c = at(f, side * 0.65f, 0.0f);
                MeshBuilder post;
                post.box({c.x - 0.02f, c.y + kTop, c.z - 0.02f}, {c.x + 0.02f, c.y + kRoof, c.z + 0.02f}, kPost);
                out.append(post);
            }
        }
    }
    // Sleepers at even spacing along the edge.
    constexpr float kSpacing = 0.17f;
    const float length = static_cast<float>(edge.length) * kUnit;
    const int count = std::max(1, static_cast<int>(length / kSpacing));
    usize seg = 0;
    for (int n = 0; n < count; ++n) {
        const float s = (static_cast<float>(n) + 0.5f) * length / static_cast<float>(count) * kTrackUnits;
        while (seg + 2 < sm.size() && static_cast<float>(sm[seg + 1].s) < s) ++seg;
        const float s0 = static_cast<float>(sm[seg].s), s1 = static_cast<float>(sm[seg + 1].s);
        const float f = s1 > s0 ? std::clamp((s - s0) / (s1 - s0), 0.0f, 1.0f) : 0.0f;
        Frame fr;
        fr.p = glm::mix(frames[seg].p, frames[seg + 1].p, f);
        fr.left = glm::normalize(glm::mix(frames[seg].left, frames[seg + 1].left, f));
        const glm::vec2 fwd(-fr.left.y, fr.left.x);
        const auto corner = [&](float side, float along) {
            return glm::vec3(fr.p.x + fr.left.x * side + fwd.x * along, fr.p.y + kSleeperTop, fr.p.z + fr.left.y * side + fwd.y * along);
        };
        constexpr float kHalfLen = 0.13f, kHalfW = 0.022f;
        out.triangleUp(corner(-kHalfLen, -kHalfW), corner(kHalfLen, kHalfW), corner(kHalfLen, -kHalfW), kSleeper);
        out.triangleUp(corner(-kHalfLen, -kHalfW), corner(-kHalfLen, kHalfW), corner(kHalfLen, kHalfW), kSleeper);
    }
}

void TrackRenderer::sync(const World& world) {
    if (revision_ == world.trackRevision) return;
    revision_ = world.trackRevision;
    // Drop meshes of removed edges, build those of new ones (edges never change once built).
    std::erase_if(meshes_, [&](const auto& kv) {
        const TrackEdge* e = world.trackEdges.tryGet(TrackEdgeId{kv.first});
        return !e || e->serial != kv.second.serial;
    });
    for (const auto [id, e] : world.trackEdges) {
        if (meshes_.contains(id.value) || e.samples.empty()) continue;
        EdgeMesh& m = meshes_[id.value];
        m.serial = e.serial;
        m.anchor = tileAt(e.samples.front().p);
        MeshBuilder mb;
        buildTrackMesh(e, m.anchor, mb);
        m.mesh.upload(mb.vertices);
        const TPoint a = e.samples.front().p, b = e.samples.back().p;
        m.centre = glm::dvec2((a.x + b.x) * 0.5 / kTrackUnits, (a.z + b.z) * 0.5 / kTrackUnits);
        m.extent = static_cast<double>(e.length) / kTrackUnits * 0.6 + 1.0;
    }
}

usize TrackRenderer::draw(const Shader& shader, const glm::dvec3& origin, const glm::dvec3& focus, double radius,
                          bool shadowPass) const {
    usize triangles = 0;
    for (const auto& [id, m] : meshes_) {
        const double dx = m.centre.x - focus.x, dz = m.centre.y - focus.z;
        if (std::sqrt(dx * dx + dz * dz) > radius + m.extent) continue;
        shader.set("uOffset", glm::vec3(static_cast<float>(m.anchor.x - origin.x), 0.0f,
                                        static_cast<float>(m.anchor.y - origin.z)));
        if (!shadowPass) shader.set("uExplored", 1.0f);
        m.mesh.draw();
        triangles += static_cast<usize>(m.mesh.vertexCount() / 3);
    }
    return triangles;
}

} // namespace tp::gfx
