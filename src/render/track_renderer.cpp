#include "render/track_renderer.hpp"

#include "render/station_style.hpp"
#include "render/terrain_mesher.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/track.hpp"
#include "sim/walking.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace tp::gfx {

namespace {

constexpr u32 kBallast = rgba(128, 118, 106);
constexpr u32 kBallastSide = rgba(112, 104, 94);
constexpr u32 kSleeper = rgba(96, 74, 56);
constexpr u32 kSteel = rgba(150, 152, 158);
constexpr u32 kPlatform = stationStyle::paving;
constexpr u32 kPlatformSide = rgba(150, 144, 134);
constexpr u32 kPlatformEdge = rgba(232, 206, 90);
constexpr u32 kCanopy = stationStyle::trim;
constexpr u32 kPost = stationStyle::metal;
constexpr u32 kPlatformLamp = rgba(255, 244, 214, 252); // self-lit (see the shaders' emission codes)
constexpr u32 kConcrete = rgba(178, 172, 162);
constexpr u32 kConcreteSide = rgba(150, 145, 136);
constexpr u32 kConcreteUnder = rgba(104, 100, 94);
constexpr u32 kStone = rgba(138, 132, 124);
constexpr u32 kPortalDark = rgba(18, 18, 20);
constexpr u32 kCrossingDeck = rgba(86, 88, 92);
// Freight stations: concrete loading docks with timber edges, a goods shed, cranes and floodlights.
constexpr u32 kDock = rgba(170, 166, 158);
constexpr u32 kDockSide = rgba(132, 128, 120);
constexpr u32 kDockJoint = rgba(140, 136, 128);
constexpr u32 kDockEdge = rgba(112, 84, 58);
constexpr u32 kShedWall = rgba(150, 78, 60);
constexpr u32 kShedRoof = rgba(86, 92, 100);
constexpr u32 kCrane = rgba(226, 182, 44);
constexpr u32 kCrate = rgba(176, 132, 82);
constexpr u32 kContainer[3] = {rgba(52, 110, 160), rgba(176, 64, 52), rgba(70, 136, 84)};

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

StationUse useOf(const World& world, const TrackEdge& edge) {
    const Station* s = edge.kind == TrackKind::Platform ? world.stations.tryGet(edge.station) : nullptr;
    return s ? s->use : StationUse::Both;
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
    if (edge.kind == TrackKind::Platform && useOf(world, edge) == StationUse::Freight) {
        // Loading docks either side of the track at wagon floor height, a goods shed along the
        // left one, crates and containers on the right, floodlights and a gantry crane.
        constexpr float kInner = 0.33f, kOuter = 1.0f, kTop = 0.07f, kFoot = -0.16f;
        const float platformLength = static_cast<float>(edge.length) / kTrackUnits;
        for (usize i = 0; i + 1 < frames.size(); ++i) {
            const Frame& a = frames[i];
            const Frame& b = frames[i + 1];
            for (const float side : {-1.0f, 1.0f}) {
                const auto quadTop = [&](float in, float outer, float dy, u32 c) {
                    out.triangleUp(at(a, side * in, dy), at(b, side * outer, dy), at(b, side * in, dy), c);
                    out.triangleUp(at(a, side * in, dy), at(a, side * outer, dy), at(b, side * outer, dy), c);
                };
                quadTop(kInner, kOuter, kTop, kDock);
                quadTop(kInner, kInner + 0.045f, kTop + 0.003f, kDockEdge);
                for (const float edgeOff : {kInner, kOuter}) {
                    const float facing = edgeOff == kInner ? -side : side;
                    out.quadFacing(at(a, side * edgeOff, kFoot), at(b, side * edgeOff, kFoot), at(b, side * edgeOff, kTop),
                                   at(a, side * edgeOff, kTop), kDockSide, glm::vec3(a.left.x * facing, 0.0f, a.left.y * facing));
                }
            }
        }
        // Slab joints every tile.
        for (float s = 1.0f; s < platformLength; s += 1.0f) {
            const auto f = frameAt(s * kTrackUnits);
            const glm::vec2 forward(-f.left.y, f.left.x);
            for (const float side : {-1.0f, 1.0f})
                orientedBox(out, at(f, side * 0.65f, 0), forward, f.left, 0.006f, 0.27f, f.p.y + kTop + 0.001f,
                            f.p.y + kTop + 0.004f, kDockJoint, kDockJoint);
        }
        // The goods shed: an open front on posts, a back wall, closed ends and a pitched roof.
        const float s0 = platformLength >= 6.0f ? 1.2f : platformLength * 0.25f;
        const float s1 = platformLength >= 6.0f ? platformLength - 1.0f : platformLength * 0.75f;
        {
            const auto f = frameAt((s0 + s1) * 0.5f * kTrackUnits);
            const glm::vec2 forward(-f.left.y, f.left.x);
            const float half = (s1 - s0) * 0.5f, y = f.p.y;
            orientedBox(out, at(f, -0.9f, 0), forward, f.left, half, 0.03f, y + kTop, y + 0.5f, kShedWall, kShedWall);
            for (const float end : {-half, half}) {
                const glm::vec3 c = at(f, -0.69f, 0) + glm::vec3(forward.x * end, 0.0f, forward.y * end);
                orientedBox(out, c, forward, f.left, 0.03f, 0.24f, y + kTop, y + 0.5f, kShedWall, kShedWall);
            }
            for (float s = s0; s <= s1 + 0.01f; s += 1.0f) {
                const auto p = frameAt(s * kTrackUnits);
                orientedBox(out, at(p, -0.47f, 0), forward, p.left, 0.02f, 0.02f, p.p.y + kTop, p.p.y + 0.5f, kPost, kPost);
            }
            // Two roof slopes meeting at a ridge over the middle of the dock.
            const auto corner = [&](float along, float across, float height) {
                const glm::vec3 c = at(f, across, height);
                return glm::vec3(c.x + forward.x * along, c.y, c.z + forward.y * along);
            };
            const float lo = 0.5f, ridge = 0.62f, h = half + 0.06f;
            out.quadFacing(corner(-h, -0.42f, lo), corner(h, -0.42f, lo), corner(h, -0.69f, ridge), corner(-h, -0.69f, ridge),
                           kShedRoof, glm::vec3(f.left.x * 0.4f, 1.0f, f.left.y * 0.4f));
            out.quadFacing(corner(-h, -0.69f, ridge), corner(h, -0.69f, ridge), corner(h, -0.96f, lo), corner(-h, -0.96f, lo),
                           shade(kShedRoof, 0.88f), glm::vec3(-f.left.x * 0.4f, 1.0f, -f.left.y * 0.4f));
            for (const float end : {-h, h})
                out.quadFacing(corner(end, -0.42f, lo), corner(end, -0.96f, lo), corner(end, -0.69f, ridge), corner(end, -0.69f, ridge),
                               kShedWall, glm::vec3(forward.x * end, 0.0f, forward.y * end));
        }
        // Freight waiting on the open dock, and floodlights along its back.
        for (float s = 0.8f; s < platformLength - 0.4f; s += 1.6f) {
            const auto f = frameAt(s * kTrackUnits);
            const glm::vec2 forward(-f.left.y, f.left.x);
            const int pick = static_cast<int>(s * 7.0f) % 3;
            const float y = f.p.y + kTop;
            if (pick == 0) {
                orientedBox(out, at(f, 0.66f, 0), forward, f.left, 0.2f, 0.12f, y, y + 0.16f, kContainer[static_cast<int>(s) % 3],
                            shade(kContainer[static_cast<int>(s) % 3], 1.1f));
            } else {
                // Crates side by side, the third stacked on top.
                for (int crate = 0; crate < 1 + pick; ++crate) {
                    const float base = y + 0.11f * static_cast<float>(crate / 2);
                    orientedBox(out, at(f, 0.6f + 0.13f * static_cast<float>(crate % 2), 0), forward, f.left, 0.055f, 0.055f, base,
                                base + 0.11f, shade(kCrate, 0.9f), kCrate);
                }
            }
        }
        for (float s = 0.5f; s < platformLength; s += 4.0f) {
            const auto f = frameAt(s * kTrackUnits);
            const glm::vec2 forward(-f.left.y, f.left.x);
            orientedBox(out, at(f, 0.89f, 0), forward, f.left, 0.014f, 0.014f, f.p.y + kTop, f.p.y + 0.85f, kPost, kPost);
            orientedBox(out, at(f, 0.84f, 0), forward, f.left, 0.06f, 0.06f, f.p.y + 0.82f, f.p.y + 0.86f, kPost, kPlatformLamp);
        }
        // A gantry crane straddling the track and both docks.
        if (platformLength >= 4.0f) {
            const auto f = frameAt(platformLength * 0.5f * kTrackUnits);
            const glm::vec2 forward(-f.left.y, f.left.x);
            const float y = f.p.y;
            for (const float side : {-0.97f, 0.97f})
                for (const float along : {-0.12f, 0.12f}) {
                    const glm::vec3 c = at(f, side, 0) + glm::vec3(forward.x * along, 0.0f, forward.y * along);
                    orientedBox(out, c, forward, f.left, 0.025f, 0.025f, y + kTop, y + 0.9f, kCrane, kCrane);
                }
            for (const float along : {-0.12f, 0.12f}) {
                const glm::vec3 c = f.p + glm::vec3(forward.x * along, 0.0f, forward.y * along);
                orientedBox(out, c, forward, f.left, 0.03f, 1.0f, y + 0.88f, y + 0.95f, kCrane, kCrane);
            }
            orientedBox(out, at(f, 0.45f, 0), forward, f.left, 0.1f, 0.07f, y + 0.8f, y + 0.88f, kPost, kPost);
            orientedBox(out, at(f, 0.45f, 0), forward, f.left, 0.006f, 0.006f, y + 0.55f, y + 0.8f, kPost, kPost);
        }
    } else if (edge.kind == TrackKind::Platform) {
        // Platforms either side of the track, with an edge line and a canopy on posts.
        constexpr float kInner = 0.33f, kOuter = 0.93f, kTop = 0.05f, kFoot = -0.16f;
        constexpr float kRoofIn = 0.42f, kRoofOut = 0.88f, kRoof = 0.46f;
        const float platformLength = static_cast<float>(edge.length) / kTrackUnits;
        const auto covered = [&](float s) { return s > 2.35f && s < platformLength - 1.2f; };
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
                const float middle = (sm[i].s + sm[i + 1].s) * 0.5f / kTrackUnits;
                if (covered(middle)) {
                    const auto roof = frameAt(middle * kTrackUnits);
                    const glm::vec2 forward(-roof.left.y, roof.left.x);
                    orientedBox(out, at(roof, side * (kRoofIn + kRoofOut) * 0.5f, 0), forward, roof.left,
                                (sm[i + 1].s - sm[i].s) * 0.5f / kTrackUnits + 0.001f,
                                (kRoofOut - kRoofIn) * 0.5f, roof.p.y + kRoof - 0.025f, roof.p.y + kRoof,
                                kCanopy, kCanopy);
                }
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
        // Paving joints and tactile studs give the platform scale without cluttering its edge.
        for (float s = 0.25f; s < platformLength; s += 0.5f) {
            const auto f = frameAt(s * kTrackUnits);
            const glm::vec2 forward(-f.left.y, f.left.x);
            for (float side : {-1.0f, 1.0f}) {
                orientedBox(out, at(f, side * 0.64f, 0), forward, f.left, 0.004f, 0.29f,
                            f.p.y + kTop + 0.001f, f.p.y + kTop + 0.003f, stationStyle::mortar,
                            stationStyle::mortar);
                for (float lateral : {0.39f, 0.415f})
                    orientedBox(out, at(f, side * lateral, 0), forward, f.left, 0.008f, 0.006f,
                                f.p.y + kTop + 0.003f, f.p.y + kTop + 0.008f, kPlatformEdge, kPlatformEdge);
            }
        }
        // Furniture sits at the back, leaving the boarding edge and stair approaches open.
        for (float s = 0.75f; s < platformLength; s += 2.0f) {
            const auto f = frameAt(s * kTrackUnits);
            const auto saved = out.transform;
            out.transform = saved * glm::translate(glm::mat4(1), f.p) *
                            glm::rotate(glm::mat4(1), std::atan2(-f.left.x, -f.left.y), glm::vec3(0, 1, 0));
            for (float side : {-1.0f, 1.0f}) {
                const float z = side * 0.84f;
                // Lamps and double-sided enamel signs remain visible in uncovered sections.
                out.box({-0.014f, kTop, z - 0.014f}, {0.014f, 0.37f, z + 0.014f}, kPost);
                out.box({-0.07f, 0.36f, z - 0.035f}, {0.07f, 0.385f, z + 0.035f}, stationStyle::trim);
                out.box({-0.052f, 0.355f, z - 0.025f}, {0.052f, 0.36f, z + 0.025f}, kPlatformLamp);
                out.box({-0.13f, 0.22f, z - 0.019f}, {0.13f, 0.31f, z + 0.019f}, stationStyle::sign);
                for (float face : {-1.0f, 1.0f}) {
                    const float front = z + face * 0.020f;
                    out.box({-0.10f, 0.263f, front - 0.001f}, {0.06f, 0.273f, front + 0.001f},
                            stationStyle::lettering);
                    out.box({-0.10f, 0.24f, front - 0.001f}, {-0.015f, 0.247f, front + 0.001f},
                            stationStyle::lettering);
                }
                if (covered(s)) {
                    out.box({-0.018f, kTop, side * 0.67f - 0.018f}, {0.018f, kRoof, side * 0.67f + 0.018f},
                            kPost);
                    out.box({-0.04f, kRoof - 0.035f, side * 0.65f - 0.22f},
                            {0.04f, kRoof - 0.015f, side * 0.65f + 0.22f}, stationStyle::stone);
                }
                if (s > 2.3f && s < platformLength - 1.4f) {
                    const float bz = side * 0.76f;
                    out.box({0.23f, 0.105f, bz - 0.065f}, {0.61f, 0.13f, bz + 0.065f}, stationStyle::wood);
                    out.box({0.23f, 0.13f, bz + side * 0.06f - 0.012f},
                            {0.61f, 0.225f, bz + side * 0.06f + 0.012f}, stationStyle::wood);
                    for (float leg : {0.27f, 0.57f})
                        out.box({leg, kTop, bz - 0.045f}, {leg + 0.02f, 0.11f, bz + 0.045f}, kPost);
                    out.box({-0.31f, kTop, bz - 0.05f}, {-0.22f, 0.18f, bz + 0.05f}, stationStyle::trim);
                    out.box({-0.315f, 0.18f, bz - 0.055f}, {-0.215f, 0.195f, bz + 0.055f},
                            stationStyle::metal);
                }
            }
            out.transform = saved;
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

// A complete station crossing, independent of which entrances or walking routes are in use.
void buildStationConcourseMesh(const World& world, StationId sid, TilePos anchor, MeshBuilder& out) {
    const auto stairs = walking::stationConcourse(world, sid);
    if (stairs.empty()) return;
    const auto origin = stairs.front().landing.p;
    const auto dir =
        normalised(i64(stairs.front().foot.p.x) - origin.x, i64(stairs.front().foot.p.z) - origin.z);
    const TDir normal{-dir.z, dir.x};
    const float unit = 1.0f / kTrackUnits, heightUnit = kHeightStep / kSubLevels;
    const auto coord = [&](TPoint p) {
        const i64 dx = i64(p.x) - origin.x, dz = i64(p.z) - origin.z;
        return glm::vec2((dx * dir.x + dz * dir.z) * unit / kDirOne,
                         (dx * normal.x + dz * normal.z) * unit / kDirOne);
    };
    const auto saved = out.transform;
    out.transform = saved * glm::translate(glm::mat4(1), glm::vec3(origin.x * unit - anchor.x, 0,
                                                                   origin.z * unit - anchor.y));
    out.transform = out.transform * glm::rotate(glm::mat4(1),
                                                static_cast<float>(std::atan2(-double(dir.z), double(dir.x))),
                                                glm::vec3(0, 1, 0));
    const float y = stairs.front().landing.height * heightUnit + 0.05f;
    float lo = 0, hi = 0;
    std::vector<float> openings;
    for (const auto& stair : stairs) {
        const float z = coord(stair.landing.p).y;
        lo = std::min(lo, z);
        hi = std::max(hi, z);
        openings.push_back(z);
    }
    lo -= 0.17f;
    hi += 0.17f;
    out.box({-0.18f, y - 0.065f, lo}, {0.18f, y, hi}, stationStyle::stone);
    for (float z = lo; z < hi; z += 0.24f)
        out.box({-0.16f, y + 0.001f, z}, {0.16f, y + 0.003f, std::min(z + 0.008f, hi)}, stationStyle::mortar);
    const auto beam = [&](glm::vec3 a, glm::vec3 b, float thickness, u32 colour) {
        const auto delta = b - a;
        const auto base = out.transform;
        out.transform = base * glm::translate(glm::mat4(1), (a + b) * 0.5f);
        out.transform =
            out.transform * glm::rotate(glm::mat4(1), std::atan2(-delta.z, delta.x), glm::vec3(0, 1, 0));
        out.transform =
            out.transform *
            glm::rotate(glm::mat4(1), std::atan2(delta.y, std::hypot(delta.x, delta.z)), glm::vec3(0, 0, 1));
        const float half = glm::length(delta) * 0.5f;
        out.box({-half, -thickness / 2, -thickness / 2}, {half, thickness / 2, thickness / 2}, colour);
        out.transform = base;
    };
    // Continuous back rail; the stair side has openings at every platform landing.
    for (const float x : {-0.18f, 0.18f}) {
        for (float z = lo; z < hi; z += 0.12f) {
            const float end = std::min(z + 0.12f, hi), mid = (z + end) * 0.5f;
            if (x > 0 && std::ranges::any_of(openings, [&](float o) { return std::abs(mid - o) < 0.16f; }))
                continue;
            for (float lift : {0.09f, 0.19f})
                beam({x, y + lift, z}, {x, y + lift, end}, 0.014f, stationStyle::trim);
            out.box({x - 0.009f, y, z - 0.009f}, {x + 0.009f, y + 0.2f, z + 0.009f}, stationStyle::metal);
        }
    }
    for (float z : {lo, hi}) beam({-0.18f, y + 0.19f, z}, {0.18f, y + 0.19f, z}, 0.014f, stationStyle::trim);
    for (const auto& stair : stairs) {
        const auto foot = coord(stair.foot.p), top = coord(stair.landing.p);
        const float base = stair.foot.height * heightUnit + 0.05f;
        const int steps = std::max(8, static_cast<int>(std::ceil((y - base) / 0.065f)));
        for (int n = 0; n < steps; ++n) {
            const float t0 = n / float(steps), t1 = (n + 1) / float(steps);
            const float x0 = foot.x + (top.x - foot.x) * t0, x1 = foot.x + (top.x - foot.x) * t1;
            const float tread = base + (y - base) * t1;
            out.box({x1, tread - 0.045f, top.y - 0.14f}, {x0, tread, top.y + 0.14f}, stationStyle::stone);
            out.box({x0 - 0.012f, tread + 0.001f, top.y - 0.14f}, {x0, tread + 0.003f, top.y + 0.14f},
                    stationStyle::lettering);
        }
        for (float side : {-0.15f, 0.15f}) {
            const float z = top.y + side;
            beam({foot.x, base - 0.025f, z}, {top.x, y - 0.025f, z}, 0.035f, stationStyle::metal);
            for (float lift : {0.09f, 0.19f})
                beam({foot.x, base + lift, z}, {top.x, y + lift, z}, 0.014f, stationStyle::trim);
            for (int n = 0; n <= 4; ++n) {
                const float t = n / 4.0f, x = foot.x + (top.x - foot.x) * t, floor = base + (y - base) * t;
                out.box({x - 0.009f, floor, z - 0.009f}, {x + 0.009f, floor + 0.2f, z + 0.009f},
                        stationStyle::metal);
            }
            out.box({top.x - 0.045f, base - 0.06f, z - 0.035f}, {top.x + 0.045f, y - 0.04f, z + 0.035f},
                    stationStyle::trim);
        }
    }
    out.transform = saved;
}

void buildRoadCurveMesh(const World& world, const RoadCurve& c, TilePos anchor, MeshBuilder& out) {
    constexpr u32 kPavement = rgba(176, 172, 162); // as the grid's road pieces (terrain_mesher.cpp)
    constexpr u32 kAsphalt = rgba(80, 82, 88);
    constexpr u32 kMarking = rgba(236, 234, 220);
    const std::vector<TrackSample>& sm = c.samples;
    if (sm.size() < 2 || c.tileStart.size() != c.tiles.size() + 1 || c.length <= 0) return;
    // Cross sections about every sixteenth of a tile across the centre line; every point stands
    // on the surface of the road tile under it (they meet at the same height at their edges), or
    // beside the road no lower than the centre line, over any higher ground there.
    struct Section {
        glm::vec2 p, left;
        float y = 0.0f;
        i32 s = 0;
    };
    std::vector<Section> sections;
    const usize step = std::max<usize>(1, static_cast<usize>(16) * (sm.size() - 1) / static_cast<usize>(c.length));
    usize tile = 0;
    for (usize i = 0;; i = std::min(i + step, sm.size() - 1)) {
        const TrackSample& at = sm[i];
        while (tile + 1 < c.tiles.size() && c.tileStart[tile + 1] <= at.s) ++tile;
        // Square to the grid at both ends, where the road pieces of the end tiles meet it.
        glm::vec2 d;
        if (i == 0 || i + 1 >= sm.size()) {
            const TilePos axis = dirOffset(i == 0 ? c.sideA : opposite(c.sideB));
            d = glm::vec2(static_cast<float>(axis.x), static_cast<float>(axis.y));
        } else {
            const TPoint before = sm[i >= 2 ? i - 2 : 0].p, after = sm[std::min(i + 2, sm.size() - 1)].p;
            d = glm::vec2(static_cast<float>(after.x - before.x), static_cast<float>(after.z - before.z));
            d = glm::length(d) > 0.0f ? glm::normalize(d) : glm::vec2(1.0f, 0.0f);
        }
        const TilePos t = c.tiles[tile];
        const glm::vec2 p(at.p.x / static_cast<float>(kTrackUnits), at.p.z / static_cast<float>(kTrackUnits));
        const float y = roadSurfaceY(world, t, std::clamp(p.x - static_cast<float>(t.x), 0.0f, 1.0f),
                                     std::clamp(p.y - static_cast<float>(t.y), 0.0f, 1.0f));
        sections.push_back({p - glm::vec2(static_cast<float>(anchor.x), static_cast<float>(anchor.y)), {d.y, -d.x}, y, at.s});
        if (i + 1 >= sm.size()) break;
    }
    const auto strip = [&](float half, float lift, u32 colour, bool dashed) {
        for (usize i = 0; i + 1 < sections.size(); ++i) {
            const Section& a = sections[i];
            const Section& b = sections[i + 1];
            if (dashed && ((a.s + b.s) / 2) % 128 >= 64) continue; // a quarter tile dashes, a quarter gaps
            const auto v = [&](const Section& q, float side) {
                const glm::vec2 xz = q.p + q.left * (half * side);
                const float y = curvedRoadY(world, c, anchor.x + static_cast<double>(xz.x), anchor.y + static_cast<double>(xz.y), q.y);
                return glm::vec3(xz.x, y + lift, xz.y);
            };
            out.triangleUp(v(a, 1.0f), v(b, -1.0f), v(a, -1.0f), colour);
            out.triangleUp(v(a, 1.0f), v(b, 1.0f), v(b, -1.0f), colour);
        }
    };
    strip(0.40f, 0.015f, kPavement, false);
    strip(0.28f, 0.025f, kAsphalt, false);
    strip(0.012f, 0.03f, kMarking, true);
}

void TrackRenderer::sync(const World& world) {
    if (roadRevision_ != world.roadCurveRevision) {
        roadRevision_ = world.roadCurveRevision;
        std::erase_if(roads_, [&](const auto& kv) {
            const RoadCurve* c = world.roadCurves.tryGet(RoadCurveId{kv.first});
            return !c || c->a != kv.second.a || c->b != kv.second.b || c->sideA != kv.second.sideA || c->sideB != kv.second.sideB;
        });
        for (const auto [id, c] : world.roadCurves) {
            if (roads_.contains(id.value) || c.samples.empty()) continue;
            RoadMesh& m = roads_[id.value];
            m.a = c.a;
            m.b = c.b;
            m.sideA = c.sideA;
            m.sideB = c.sideB;
            m.anchor = c.a;
            MeshBuilder mb;
            buildRoadCurveMesh(world, c, m.anchor, mb);
            m.mesh.upload(mb.vertices);
            m.centre = glm::dvec2((c.a.x + c.b.x) * 0.5 + 0.5, (c.a.y + c.b.y) * 0.5 + 0.5);
            m.extent = static_cast<double>(c.length) / kTrackUnits * 0.6 + 1.0;
        }
    }
    if (revision_ == world.trackRevision) return;
    revision_ = world.trackRevision;
    concourses_.clear();
    for (const auto [sid, station] : world.stations) {
        if (!station.takesPassengers()) continue; // freight docks have no footbridge
        const auto stairs = walking::stationConcourse(world, sid);
        if (stairs.empty()) continue;
        auto& m = concourses_[sid.value];
        m.anchor = tileAt(stairs.front().landing.p);
        MeshBuilder mesh;
        buildStationConcourseMesh(world, sid, m.anchor, mesh);
        m.mesh.upload(mesh.vertices);
        m.centre = glm::dvec2(m.anchor.x, m.anchor.y);
        for (const auto& stair : stairs)
            m.extent = std::max(m.extent,
                                distance(stairs.front().landing.p, stair.foot.p) / double(kTrackUnits) + 1);
    }
    // Drop meshes of removed edges, build those of new ones (edges never change once built).
    std::erase_if(meshes_, [&](const auto& kv) {
        const TrackEdge* e = world.trackEdges.tryGet(TrackEdgeId{kv.first});
        return !e || e->serial != kv.second.serial || useOf(world, *e) != kv.second.use;
    });
    for (const auto [id, e] : world.trackEdges) {
        if (meshes_.contains(id.value) || e.samples.empty()) continue;
        EdgeMesh& m = meshes_[id.value];
        m.serial = e.serial;
        m.use = useOf(world, e);
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
    const auto drawMeshes = [&](const auto& meshes) {
        for (const auto& [id, m] : meshes) {
            const double dx = m.centre.x - focus.x, dz = m.centre.y - focus.z;
            if (std::sqrt(dx * dx + dz * dz) > radius + m.extent) continue;
            shader.set("uOffset", glm::vec3(static_cast<float>(m.anchor.x - origin.x), 0.0f,
                                            static_cast<float>(m.anchor.y - origin.z)));
            if (!shadowPass) shader.set("uExplored", 1.0f);
            m.mesh.draw();
            triangles += static_cast<usize>(m.mesh.vertexCount() / 3);
        }
    };
    drawMeshes(meshes_);
    drawMeshes(concourses_);
    drawMeshes(roads_);
    return triangles;
}

} // namespace tp::gfx
