#include "render/track_renderer.hpp"

#include "render/terrain_mesher.hpp"
#include "sim/track.hpp"

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
constexpr u32 kConcrete = rgba(178, 172, 162);
constexpr u32 kConcreteSide = rgba(150, 145, 136);
constexpr u32 kConcreteUnder = rgba(104, 100, 94);
constexpr u32 kStone = rgba(138, 132, 124);
constexpr u32 kPortalDark = rgba(18, 18, 20);
constexpr u32 kCrossingDeck = rgba(86, 88, 92);

constexpr float kBallastTop = -0.035f; // relative to the nominal rail height
constexpr float kSleeperTop = -0.027f;
constexpr float kRailTop = -0.017f;
constexpr float kBallastHalf = 0.27f;
constexpr float kBallastFoot = 0.42f;
constexpr float kGauge = 0.07f;
constexpr float kRailHalf = 0.012f;

constexpr float kDeckHalf = 0.38f;  // bridge deck, either side of the centre line
constexpr float kDeckDepth = 0.22f; // below the ballast top

struct Frame {
    glm::vec3 p;     // centre line position (rail height)
    glm::vec2 left;  // unit vector to the left of the direction of travel
};

// A box standing upright, turned to follow the track (fwd and left are unit vectors).
void orientedBox(MeshBuilder& out, glm::vec3 c, glm::vec2 fwd, glm::vec2 left, float halfAlong, float halfAcross, float y0,
                 float y1, u32 colour, u32 top) {
    const auto p = [&](float a, float l, float y) {
        return glm::vec3(c.x + fwd.x * a + left.x * l, y, c.z + fwd.y * a + left.y * l);
    };
    out.triangleUp(p(-halfAlong, -halfAcross, y1), p(halfAlong, halfAcross, y1), p(halfAlong, -halfAcross, y1), top);
    out.triangleUp(p(-halfAlong, -halfAcross, y1), p(-halfAlong, halfAcross, y1), p(halfAlong, halfAcross, y1), top);
    for (const float sa : {-1.0f, 1.0f}) {
        out.quadFacing(p(sa * halfAlong, -halfAcross, y0), p(sa * halfAlong, halfAcross, y0), p(sa * halfAlong, halfAcross, y1),
                       p(sa * halfAlong, -halfAcross, y1), colour, glm::vec3(fwd.x * sa, 0.0f, fwd.y * sa));
        out.quadFacing(p(-halfAlong, sa * halfAcross, y0), p(halfAlong, sa * halfAcross, y0), p(halfAlong, sa * halfAcross, y1),
                       p(-halfAlong, sa * halfAcross, y1), colour, glm::vec3(left.x * sa, 0.0f, left.y * sa));
    }
}

} // namespace

float trackRailY(i32 fineHeight) { return static_cast<float>(fineHeight) * (kHeightStep / kSubLevels) + kRailTop; }

void buildTrackMesh(const World& world, const TrackEdge& edge, TilePos anchor, MeshBuilder& out) {
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
    // The frame `s` units along the edge.
    const auto frameAt = [&](float s) {
        usize seg = 0;
        while (seg + 2 < sm.size() && static_cast<float>(sm[seg + 1].s) < s) ++seg;
        const float s0 = static_cast<float>(sm[seg].s), s1 = static_cast<float>(sm[seg + 1].s);
        const float f = s1 > s0 ? std::clamp((s - s0) / (s1 - s0), 0.0f, 1.0f) : 0.0f;
        Frame fr;
        fr.p = glm::mix(frames[seg].p, frames[seg + 1].p, f);
        fr.left = glm::normalize(glm::mix(frames[seg].left, frames[seg + 1].left, f));
        return fr;
    };
    // What carries each segment.
    const usize segments = sm.size() - 1;
    std::vector<TrackStructure> kind(segments, TrackStructure::Ground);
    std::vector<u8> crossing(segments, 0);
    for (usize i = 0; i < segments; ++i) {
        kind[i] = track::structureAt(edge.spans, (sm[i].s + sm[i + 1].s) / 2);
        if (kind[i] == TrackStructure::Ground && !edge.crossings.empty()) {
            const TPoint mid{(sm[i].p.x + sm[i + 1].p.x) / 2, (sm[i].p.z + sm[i + 1].p.z) / 2};
            crossing[i] = std::ranges::find(edge.crossings, tileAt(mid)) != edge.crossings.end();
        }
    }
    // Where the ground under the bed was left higher (beside roads and buildings, whose ground
    // stays as it is), the ballast rises just clear of it.
    for (usize j = 0; j < frames.size(); ++j) {
        const auto structure = [&](usize seg) { return seg < segments && kind[seg] != TrackStructure::Ground; };
        if (structure(j) || (j > 0 && structure(j - 1))) continue;
        const float ground = static_cast<float>(track::terrainAt(world, sm[j].p)) * k;
        frames[j].p.y = std::max(frames[j].p.y, ground + 0.012f - kBallastTop);
    }
    for (usize i = 0; i + 1 < frames.size(); ++i) {
        const Frame& a = frames[i];
        const Frame& b = frames[i + 1];
        const auto strip = [&](float in, float outer, float dy, u32 c) {
            out.triangleUp(at(a, in, dy), at(b, outer, dy), at(b, in, dy), c);
            out.triangleUp(at(a, in, dy), at(a, outer, dy), at(b, outer, dy), c);
        };
        const auto slope = [&](float in, float dyIn, float outer, float dyOut, u32 c) {
            for (const float side : {-1.0f, 1.0f}) {
                const glm::vec3 a0 = at(a, side * in, dyIn), b0 = at(b, side * in, dyIn);
                const glm::vec3 a1 = at(a, side * outer, dyOut), b1 = at(b, side * outer, dyOut);
                out.triangleUp(a0, b0, b1, c);
                out.triangleUp(a0, b1, a1, c);
            }
        };
        if (crossing[i]) {
            // Level crossing: a deck just above the road surface, between and beside the rails.
            const auto deck = [&](usize j) {
                const float road = static_cast<float>(track::terrainAt(world, sm[j].p)) * k + 0.03f;
                return std::max(kRailTop - 0.004f, road - frames[j].p.y);
            };
            const float da = deck(i), db = deck(i + 1);
            out.triangleUp(at(a, -0.46f, da), at(b, 0.46f, db), at(b, -0.46f, db), kCrossingDeck);
            out.triangleUp(at(a, -0.46f, da), at(a, 0.46f, da), at(b, 0.46f, db), kCrossingDeck);
            for (const float side : {-1.0f, 1.0f}) {
                const glm::vec3 a0 = at(a, side * 0.46f, da), b0 = at(b, side * 0.46f, db);
                const glm::vec3 a1 = at(a, side * 0.62f, kBallastTop - 0.17f), b1 = at(b, side * 0.62f, kBallastTop - 0.17f);
                out.triangleUp(a0, b0, b1, kCrossingDeck);
                out.triangleUp(a0, b1, a1, kCrossingDeck);
            }
            for (const float rail : {-kGauge, kGauge}) {
                const float ra = std::max(kRailTop, da + 0.008f), rb = std::max(kRailTop, db + 0.008f);
                out.triangleUp(at(a, rail - kRailHalf, ra), at(b, rail + kRailHalf, rb), at(b, rail - kRailHalf, rb), kSteel);
                out.triangleUp(at(a, rail - kRailHalf, ra), at(a, rail + kRailHalf, ra), at(b, rail + kRailHalf, rb), kSteel);
            }
            continue;
        } else if (kind[i] == TrackStructure::Bridge) {
            // Bridge deck: ballast in a concrete trough with parapets, on a slab.
            strip(-kBallastHalf, kBallastHalf, kBallastTop, kBallast);
            strip(kBallastHalf, kDeckHalf, kBallastTop - 0.004f, kConcrete);
            strip(-kDeckHalf, -kBallastHalf, kBallastTop - 0.004f, kConcrete);
            for (const float side : {-1.0f, 1.0f}) {
                const glm::vec3 normal(a.left.x * side, 0.0f, a.left.y * side);
                out.quadFacing(at(a, side * kDeckHalf, kBallastTop - kDeckDepth), at(b, side * kDeckHalf, kBallastTop - kDeckDepth),
                               at(b, side * kDeckHalf, kBallastTop + 0.07f), at(a, side * kDeckHalf, kBallastTop + 0.07f),
                               kConcreteSide, normal);
                out.quadFacing(at(a, side * (kDeckHalf - 0.03f), kBallastTop), at(b, side * (kDeckHalf - 0.03f), kBallastTop),
                               at(b, side * (kDeckHalf - 0.03f), kBallastTop + 0.07f),
                               at(a, side * (kDeckHalf - 0.03f), kBallastTop + 0.07f), kConcreteSide, -normal);
                const float o = side > 0 ? kDeckHalf - 0.03f : -kDeckHalf;
                strip(o, o + 0.03f, kBallastTop + 0.07f, kConcrete);
            }
            out.quadFacing(at(a, -kDeckHalf, kBallastTop - kDeckDepth), at(b, -kDeckHalf, kBallastTop - kDeckDepth),
                           at(b, kDeckHalf, kBallastTop - kDeckDepth), at(a, kDeckHalf, kBallastTop - kDeckDepth), kConcreteUnder,
                           glm::vec3(0.0f, -1.0f, 0.0f));
        } else if (kind[i] == TrackStructure::Tunnel) {
            strip(-kBallastHalf, kBallastHalf, kBallastTop, kBallast);
        } else {
            // Ballast: a flat top with sloping shoulders.
            strip(-kBallastHalf, kBallastHalf, kBallastTop, kBallast);
            slope(kBallastHalf, kBallastTop, kBallastFoot, kBallastTop - 0.17f, kBallastSide);
        }
        // Rails.
        for (const float rail : {-kGauge, kGauge}) strip(rail - kRailHalf, rail + kRailHalf, kRailTop, kSteel);
    }
    // Bridge piers every couple of tiles, and abutments where a bridge meets the ground.
    for (const TrackSpan& sp : edge.spans) {
        if (sp.kind != TrackStructure::Bridge) continue;
        std::vector<std::pair<float, bool>> supports; // distance along the edge, abutment
        if (sp.s0 > 0) supports.push_back({static_cast<float>(sp.s0), true});
        if (sp.s1 < edge.length) supports.push_back({static_cast<float>(sp.s1), true});
        const float spanLength = static_cast<float>(sp.s1 - sp.s0);
        const int piers = static_cast<int>(spanLength / (2.0f * kTrackUnits));
        for (int n = 1; n <= piers; ++n) supports.push_back({sp.s0 + spanLength * n / (piers + 1), false});
        for (const auto& [s, abutment] : supports) {
            const Frame f = frameAt(s);
            const glm::vec2 fwd(-f.left.y, f.left.x);
            const TPoint ground{static_cast<i32>(std::lround((f.p.x + anchor.x) * kTrackUnits)),
                                static_cast<i32>(std::lround((f.p.z + anchor.y) * kTrackUnits))};
            const float base = static_cast<float>(track::terrainAt(world, ground)) * k - 0.15f;
            const float top = f.p.y + kBallastTop - kDeckDepth;
            if (base >= top) continue;
            orientedBox(out, f.p, fwd, f.left, abutment ? 0.14f : 0.09f, abutment ? 0.42f : 0.3f, base, top, kConcreteSide,
                        kConcrete);
        }
    }
    // Tunnel portals: a stone arch at each end of a tunnel, facing out of the hill.
    for (const TrackSpan& sp : edge.spans) {
        if (sp.kind != TrackStructure::Tunnel) continue;
        for (const auto& [s, outward] : {std::pair{static_cast<float>(sp.s0), -1.0f}, std::pair{static_cast<float>(sp.s1), 1.0f}}) {
            const Frame f = frameAt(s);
            const glm::vec2 fwd(-f.left.y, f.left.x);
            // A stone headwall around the opening, standing proud of the hillside.
            const float y0 = f.p.y + kBallastTop - 0.15f, top = f.p.y + 1.15f;
            for (const float side : {-1.0f, 1.0f})
                orientedBox(out, at(f, side * 0.52f, 0.0f), fwd, f.left, 0.08f, 0.18f, y0, top, kStone, kStone);
            orientedBox(out, f.p, fwd, f.left, 0.08f, 0.36f, f.p.y + 0.48f, top, kStone, kStone);
            orientedBox(out, at(f, 0.0f, 0.0f), fwd, f.left, 0.11f, 0.74f, top, top + 0.05f, kConcreteSide, kConcrete);
            const glm::vec2 in = fwd * (-outward * 0.02f);
            const auto o = [&](float side, float dy) {
                const glm::vec3 q = at(f, side, dy);
                return glm::vec3(q.x + in.x, q.y, q.z + in.y);
            };
            out.quadFacing(o(-0.34f, kBallastTop), o(0.34f, kBallastTop), o(0.34f, 0.48f), o(-0.34f, 0.48f), kPortalDark,
                           glm::vec3(fwd.x * outward, 0.0f, fwd.y * outward));
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
        if (crossing[seg] || kind[seg] == TrackStructure::Tunnel) continue;
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
        buildTrackMesh(world, e, m.anchor, mb);
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
