#include "render/terrain_mesher.hpp"
#include "render/station_style.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/rail.hpp"
#include "sim/road.hpp"
#include "sim/station.hpp"
#include "sim/world.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <optional>

namespace tp::gfx {

namespace {
FineCorners toFine(const TileCorners& c) { return FineCorners{{fineOf(c.h[0]), fineOf(c.h[1]), fineOf(c.h[2]), fineOf(c.h[3])}}; }
} // namespace

bool tileSplitNESW(const TileCorners& c) { return toFine(c).splitNESW(); }
bool tileSplitNESW(const FineCorners& c) { return c.splitNESW(); }

float tileSurfaceLevel(const TileCorners& c, float fx, float fy) { return tileSurfaceLevel(toFine(c), fx, fy); }

float tileSurfaceLevel(const FineCorners& c, float fx, float fy) {
    constexpr float k = 1.0f / kSubLevels;
    const float nw = c.h[0] * k, ne = c.h[1] * k, se = c.h[2] * k, sw = c.h[3] * k;
    if (!c.splitNESW()) {
        if (fx >= fy) return nw + (ne - nw) * fx + (se - ne) * fy;
        return nw + (se - sw) * fx + (sw - nw) * fy;
    }
    if (fx + fy <= 1.0f) return nw + (ne - nw) * fx + (sw - nw) * fy;
    return se + (sw - se) * (1.0f - fx) + (ne - se) * (1.0f - fy);
}

namespace {

constexpr u32 kGrass = rgba(104, 166, 76);
constexpr u32 kForestFloor = rgba(82, 138, 60);
constexpr u32 kRough = rgba(122, 158, 76);
constexpr u32 kRock = rgba(138, 132, 126);
constexpr u32 kSand = rgba(226, 210, 152);
constexpr u32 kSnow = rgba(238, 243, 250);
constexpr u32 kSeabed = rgba(178, 168, 122);
constexpr u32 kDesert = rgba(222, 182, 118);
constexpr u32 kSteppe = rgba(176, 172, 92);
constexpr u32 kTundra = rgba(140, 146, 112);
constexpr u32 kJungle = rgba(72, 140, 58);
constexpr u32 kJungleFloor = rgba(56, 116, 48);
constexpr u32 kGarden = rgba(96, 156, 70);
constexpr u32 kPlaza = rgba(170, 164, 150);
constexpr u32 kPavement = rgba(176, 172, 162);
constexpr u32 kAsphalt = rgba(80, 82, 88);
constexpr u32 kMarking = rgba(236, 234, 220);
constexpr u32 kFoundation = rgba(150, 134, 112);
constexpr u32 kUrbanRoad = rgba(112, 112, 114);
constexpr u32 kUrbanBuilding = rgba(160, 152, 140);
constexpr u32 kBallast = rgba(128, 118, 106);
constexpr u32 kRail = rgba(88, 88, 94);
constexpr u32 kStopMarking = rgba(232, 196, 64);
constexpr u32 kSleeper = rgba(96, 74, 56);
constexpr u32 kSteel = rgba(150, 152, 158);
constexpr u32 kPlatform = rgba(186, 180, 168);
constexpr u32 kPlatformEdge = rgba(226, 204, 92);

u32 groundColor(const Tile& t) {
    switch (t.ground) {
    case Ground::Grass: return t.trees >= 2 ? kForestFloor : kGrass;
    case Ground::Rough: return kRough;
    case Ground::Rock: return kRock;
    case Ground::Sand: return kSand;
    case Ground::Snow: return kSnow;
    case Ground::Water: return kSeabed;
    case Ground::Desert: return kDesert;
    case Ground::Steppe: return kSteppe;
    case Ground::Tundra: return kTundra;
    case Ground::Jungle: return t.trees >= 2 ? kJungleFloor : kJungle;
    }
    return kGrass;
}

float seabedY(i64 raw) {
    const float levels = std::max(static_cast<float>(raw) / static_cast<float>(kHeightOne), -14.0f);
    return std::min(levels, 0.0f) * 0.22f - 0.3f;
}

float jitter(u32 h) { return 0.93f + static_cast<float>(h & 0xFF) / 255.0f * 0.12f; }
// Open ground varies less from face to face: the colours blend between tiles instead.
float softJitter(u32 h) { return 0.975f + static_cast<float>(h & 0xFF) / 255.0f * 0.05f; }

// Smooth value noise in [0, 1) on a lattice `cell` tiles across, for gentle colour variation.
float softNoise(u64 seed, u32 salt, i32 x, i32 y, i32 cell) {
    const i32 ix = floorDiv(x, cell), iy = floorDiv(y, cell);
    const auto fade = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float sx = fade(static_cast<float>(x - ix * cell) / static_cast<float>(cell));
    const float sy = fade(static_cast<float>(y - iy * cell) / static_cast<float>(cell));
    const auto v = [&](i32 a, i32 b) { return static_cast<float>(hashCoords(seed, a, b, salt) & 0xFFFF) / 65536.0f; };
    const float top = v(ix, iy) + (v(ix + 1, iy) - v(ix, iy)) * sx;
    const float bottom = v(ix, iy + 1) + (v(ix + 1, iy + 1) - v(ix, iy + 1)) * sx;
    return top + (bottom - top) * sy;
}

// Open ground: drawn in its own colour, blended with its neighbours' at the corners.
bool openGround(const Tile& t) { return t.type == TileType::Clear || t.type == TileType::Track; }

// How much of each kind of ground cover grows on a tile (before the density setting): meadows
// thick with grass and drifts of flowers, ferns under the trees, heather on the heath and the
// tundra, dry grass on the steppe, stones on rock and in the desert, reeds by the water, and
// nothing at all on the snow.
struct CoverMix {
    float grass = 0, flowers = 0, ferns = 0, reeds = 0, heather = 0, pebbles = 0;
};
CoverMix coverMix(const Tile& t, bool byWater, float meadow) {
    CoverMix m;
    // Flowers drift across the meadows in patches.
    const float drift = meadow > 0.5f ? (meadow - 0.5f) * 16.0f : 0.3f;
    switch (t.ground) {
    case Ground::Grass:
        if (t.trees >= 2) m = {4.0f, 0.3f, 3.0f};
        else m = {t.trees ? 11.0f : 16.0f, drift};
        break;
    case Ground::Rough: m = {8.0f, drift * 0.4f, 0.0f, 0.0f, 3.0f, 0.3f}; break;
    case Ground::Steppe: m = {10.0f, drift * 0.3f, 0.0f, 0.0f, 0.0f, 0.4f}; break;
    case Ground::Jungle: m = {8.0f, drift * 0.6f, t.trees ? 5.0f : 2.5f}; break;
    case Ground::Tundra: m = {3.0f, drift * 0.4f, 0.0f, 0.0f, 5.0f, 0.6f}; break;
    case Ground::Desert: m = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f}; break;
    case Ground::Sand: m = {1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f}; break;
    case Ground::Rock: m = {0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 2.5f}; break;
    case Ground::Snow:
    case Ground::Water: return m;
    }
    if (byWater) m.reeds = 1.5f;
    return m;
}

// Scatters a tile's ground cover. Each piece takes the ground's colour where it stands (blended
// from the tile's corners), so grass reads as the lit tips of the turf.
void scatterCover(std::vector<CoverPlacement>& out, const Tile& tile, TilePos wp, float x0, float z0, const FineCorners& fine,
                  u64 seed, float density, bool byWater, std::array<u32, 4> corners, u32 base) {
    const float meadow = softNoise(seed, 0x7E62, wp.x, wp.y, 10);
    const CoverMix mix = coverMix(tile, byWater, meadow);
    // The flower colour of each drift, with a few strays of other colours.
    const int palette = static_cast<int>(softNoise(seed, 0x7E63, wp.x, wp.y, 24) * 4.0f) % 4;
    const auto colourAt = [&](float u, float v) {
        const auto rgb = [&](int k) { return unpackRgb(corners[static_cast<usize>(k)] ? corners[static_cast<usize>(k)] : base); };
        return glm::mix(glm::mix(rgb(0), rgb(1), u), glm::mix(rgb(3), rgb(2), u), v);
    };
    u32 salt = 0;
    const auto place = [&](Cover kind, float amount, int variants, float size) {
        const u32 h0 = hashCoords(seed, wp.x, wp.y, 0x7C00 + static_cast<u32>(kind));
        const int count = static_cast<int>(amount * density + static_cast<float>(h0 & 0xFF) / 255.0f);
        for (int k = 0; k < count; ++k) {
            const u32 h = hashCoords(seed, wp.x, wp.y, 0x7C10 + salt++);
            const float u = 0.04f + static_cast<float>(h & 0xFF) / 255.0f * 0.92f;
            const float v = 0.04f + static_cast<float>((h >> 8) & 0xFF) / 255.0f * 0.92f;
            CoverPlacement c;
            c.pos = glm::vec3(x0 + u, tileSurfaceLevel(fine, u, v) * kHeightStep, z0 + v);
            c.yaw = static_cast<float>((h >> 16) & 0xFF) / 255.0f * glm::two_pi<float>();
            c.scale = size * (0.75f + static_cast<float>((h >> 24) & 0xFF) / 255.0f * 0.55f);
            c.kind = kind;
            c.variant = static_cast<u8>(kind == Cover::Flowers && (h >> 5) % 5 ? palette : (h >> 3) % static_cast<u32>(variants));
            // Blades a little darker and richer than the turf, the lighter tips catching the sun.
            const glm::vec3 ground = colourAt(u, v) * (0.82f + static_cast<float>((h >> 12) & 0x3F) / 63.0f * 0.2f);
            c.tint = (rgbaf(ground) & 0x00FFFFFFu) | (249u << 24);
            out.push_back(c);
        }
    };
    place(Cover::Grass, mix.grass, 3, 1.35f);
    place(Cover::Flowers, mix.flowers, 4, 1.0f);
    place(Cover::Fern, mix.ferns, 2, 1.1f);
    place(Cover::Reeds, mix.reeds, 2, 1.0f);
    place(Cover::Heather, mix.heather, 2, 1.0f);
    place(Cover::Pebbles, mix.pebbles, 2, 1.0f);
}

// Ground under a building: gardens for homes, paving for everything else.
u32 buildingGround(const Tile& t, const World* world) {
    if (!world) return kGarden;
    const Building* b = world->buildings.tryGet(BuildingId(t.index));
    if (!b) return kGarden;
    return b->info().category == BuildingCategory::Residential || b->type == 12 || (b->type >= 15 && b->type <= 18) ? kGarden : kPlaza;
}

// Describes how the top of a structure tile is shaped.
struct TopSurface {
    Surface surface = Surface::Flat;
    TileCorners corners;
    u8 level = 0; // flat / foundation level
    float y(float u, float v) const {
        if (surface == Surface::Ramp) return tileSurfaceLevel(corners, u, v) * kHeightStep;
        return static_cast<float>(level) * kHeightStep;
    }
};

void quadOnSurface(MeshBuilder& mb, const TopSurface& s, float x0, float z0, float u0, float v0, float u1, float v1,
                   float lift, u32 color) {
    const auto p = [&](float u, float v) { return glm::vec3(x0 + u, s.y(u, v) + lift, z0 + v); };
    mb.triangleUp(p(u0, v0), p(u1, v1), p(u1, v0), color);
    mb.triangleUp(p(u0, v0), p(u0, v1), p(u1, v1), color);
}

void addRoad(MeshBuilder& mb, const TopSurface& s, float x0, float z0, u8 roadBits, u8 tramBits, bool stop) {
    constexpr float p0 = 0.1f, p1 = 0.9f;   // pavement / track bed extent
    constexpr float a0 = 0.22f, a1 = 0.78f; // asphalt extent
    const u8 all = static_cast<u8>(roadBits | tramBits);
    const bool n = all & dirBit(Dir::N), e = all & dirBit(Dir::E);
    const bool so = all & dirBit(Dir::S), w = all & dirBit(Dir::W);
    const u32 bed = roadBits ? kPavement : kBallast;
    quadOnSurface(mb, s, x0, z0, p0, p0, p1, p1, 0.015f, bed);
    if (n) quadOnSurface(mb, s, x0, z0, p0, 0.0f, p1, p0, 0.015f, bed);
    if (so) quadOnSurface(mb, s, x0, z0, p0, p1, p1, 1.0f, 0.015f, bed);
    if (w) quadOnSurface(mb, s, x0, z0, 0.0f, p0, p0, p1, 0.015f, bed);
    if (e) quadOnSurface(mb, s, x0, z0, p1, p0, 1.0f, p1, 0.015f, bed);
    if (roadBits) {
        const bool rn = roadBits & dirBit(Dir::N), re = roadBits & dirBit(Dir::E);
        const bool rs = roadBits & dirBit(Dir::S), rw = roadBits & dirBit(Dir::W);
        quadOnSurface(mb, s, x0, z0, a0, a0, a1, a1, 0.025f, kAsphalt);
        if (rn) quadOnSurface(mb, s, x0, z0, a0, 0.0f, a1, a0, 0.025f, kAsphalt);
        if (rs) quadOnSurface(mb, s, x0, z0, a0, a1, a1, 1.0f, 0.025f, kAsphalt);
        if (rw) quadOnSurface(mb, s, x0, z0, 0.0f, a0, a0, a1, 0.025f, kAsphalt);
        if (re) quadOnSurface(mb, s, x0, z0, a1, a0, 1.0f, a1, 0.025f, kAsphalt);
        // Dashed centre line on straight roads (not at stops).
        constexpr float m0 = 0.488f, m1 = 0.512f;
        if (!stop && roadBits == (dirBit(Dir::N) | dirBit(Dir::S)) && !tramBits) {
            quadOnSurface(mb, s, x0, z0, m0, 0.08f, m1, 0.33f, 0.03f, kMarking);
            quadOnSurface(mb, s, x0, z0, m0, 0.58f, m1, 0.83f, 0.03f, kMarking);
        } else if (!stop && roadBits == (dirBit(Dir::E) | dirBit(Dir::W)) && !tramBits) {
            quadOnSurface(mb, s, x0, z0, 0.08f, m0, 0.33f, m1, 0.03f, kMarking);
            quadOnSurface(mb, s, x0, z0, 0.58f, m0, 0.83f, m1, 0.03f, kMarking);
        }
    }
    if (stop) {
        // Yellow stop bay lines along both kerbs.
        if (all & dirBit(Dir::N)) {
            quadOnSurface(mb, s, x0, z0, 0.235f, 0.15f, 0.255f, 0.85f, 0.032f, kStopMarking);
            quadOnSurface(mb, s, x0, z0, 0.745f, 0.15f, 0.765f, 0.85f, 0.032f, kStopMarking);
        } else {
            quadOnSurface(mb, s, x0, z0, 0.15f, 0.235f, 0.85f, 0.255f, 0.032f, kStopMarking);
            quadOnSurface(mb, s, x0, z0, 0.15f, 0.745f, 0.85f, 0.765f, 0.032f, kStopMarking);
        }
    }
    // Tram rails: one track per lane, from the tile centre to each connected edge.
    constexpr float lane = 0.2f, gauge = 0.055f, rw = 0.012f;
    for (const Dir d : kDirs) {
        if (!(tramBits & dirBit(d))) continue;
        for (const float side : {-lane, lane}) {
            for (const float rail : {-gauge, gauge}) {
                const float c = 0.5f + side + rail;
                switch (d) {
                case Dir::N: quadOnSurface(mb, s, x0, z0, c - rw, 0.0f, c + rw, 0.5f + lane, 0.035f, kRail); break;
                case Dir::S: quadOnSurface(mb, s, x0, z0, c - rw, 0.5f - lane, c + rw, 1.0f, 0.035f, kRail); break;
                case Dir::W: quadOnSurface(mb, s, x0, z0, 0.0f, c - rw, 0.5f + lane, c + rw, 0.035f, kRail); break;
                case Dir::E: quadOnSurface(mb, s, x0, z0, 0.5f - lane, c - rw, 1.0f, c + rw, 0.035f, kRail); break;
                }
            }
        }
    }
}

// Quad with arbitrary corners (tile local u/v) draped over the surface.
void quadUV(MeshBuilder& mb, const TopSurface& s, float x0, float z0, glm::vec2 a, glm::vec2 b, glm::vec2 c,
            glm::vec2 d, float lift, u32 color) {
    const auto p = [&](glm::vec2 q) {
        return glm::vec3(x0 + q.x, s.y(std::clamp(q.x, 0.0f, 1.0f), std::clamp(q.y, 0.0f, 1.0f)) + lift, z0 + q.y);
    };
    mb.triangleUp(p(a), p(b), p(c), color);
    mb.triangleUp(p(a), p(c), p(d), color);
}

glm::vec2 edgeMidpoint(Dir d) {
    switch (d) {
    case Dir::N: return {0.5f, 0.0f};
    case Dir::E: return {1.0f, 0.5f};
    case Dir::S: return {0.5f, 1.0f};
    case Dir::W: return {0.0f, 0.5f};
    }
    return {0.5f, 0.5f};
}

// Railway track pieces: ballast, sleepers and two rails per piece. Strips end flush with the tile
// edges so that diagonal chains of corner pieces join up seamlessly.
// With `half` set, only the half of each piece next to that edge is drawn (depot entrances).
void addRail(MeshBuilder& mb, const TopSurface& s, float x0, float z0, u8 bits, std::optional<Dir> half = {}) {
    constexpr float kBed = 0.2f, kGauge = 0.07f, kRailW = 0.012f;
    for (u8 bit = 1; bit < 0x40; bit = static_cast<u8>(bit << 1)) {
        if (!(bits & bit)) continue;
        TrackEdges e = trackEdges(bit);
        if (half && e.b == *half) std::swap(e.a, e.b);
        const glm::vec2 a = edgeMidpoint(e.a);
        glm::vec2 b = edgeMidpoint(e.b);
        if (half) b = (a + b) * 0.5f;
        const glm::vec2 dir = glm::normalize(b - a);
        const glm::vec2 n(-dir.y, dir.x);
        const float stretch = isStraightTrack(bit) ? 1.0f : 1.41421356f;
        // Offset along an edge for a lateral distance `w` from the centre line.
        const auto along = [&](Dir edge, float w) {
            const glm::vec2 t = (edge == Dir::N || edge == Dir::S) ? glm::vec2(1, 0) : glm::vec2(0, 1);
            return t * (glm::dot(n, t) >= 0.0f ? 1.0f : -1.0f) * w * stretch;
        };
        const auto strip = [&](float w0, float w1, float lift, u32 color) {
            const Dir eb = half ? e.a : e.b;
            quadUV(mb, s, x0, z0, a + along(e.a, w0), a + along(e.a, w1), b + along(eb, w1), b + along(eb, w0), lift,
                   color);
        };
        strip(-kBed, kBed, 0.018f, kBallast);
        {
            const float len = glm::length(b - a);
            const int count = (isStraightTrack(bit) ? 6 : 4) / (half ? 2 : 1);
            for (int k = 0; k < count; ++k) {
                const glm::vec2 c = a + dir * (len * (static_cast<float>(k) + 0.5f) / static_cast<float>(count));
                const glm::vec2 h = n * 0.13f, t = dir * 0.022f;
                quadUV(mb, s, x0, z0, c - h - t, c + h - t, c + h + t, c - h + t, 0.026f, kSleeper);
            }
        }
        for (const float side : {-kGauge, kGauge}) strip(side - kRailW, side + kRailW, 0.036f, kSteel);
    }
}

// Platform slabs either side of a rail station's track.
void addPlatforms(MeshBuilder& mb, float x0, float z0, float y, u8 bit) {
    constexpr float kH = 0.1f;
    const bool ns = bit == kTrackNS;
    for (const float side : {0.0f, 1.0f}) {
        const float a0 = side == 0.0f ? 0.03f : 0.72f, a1 = side == 0.0f ? 0.28f : 0.97f;
        const float e0 = side == 0.0f ? 0.25f : 0.72f, e1 = side == 0.0f ? 0.28f : 0.75f;
        if (ns) {
            mb.box({x0 + a0, y, z0}, {x0 + a1, y + kH, z0 + 1.0f}, kPlatform, 0x37);
            mb.box({x0 + e0, y + kH, z0}, {x0 + e1, y + kH + 0.004f, z0 + 1.0f}, kPlatformEdge, 0x04);
        } else {
            mb.box({x0, y, z0 + a0}, {x0 + 1.0f, y + kH, z0 + a1}, kPlatform, 0x37);
            mb.box({x0, y + kH, z0 + e0}, {x0 + 1.0f, y + kH + 0.004f, z0 + e1}, kPlatformEdge, 0x04);
        }
    }
}

// Raised flat foundation: top quad plus side walls down to the terrain.
void addFoundation(MeshBuilder& mb, const TileCorners& c, float x0, float z0, float topY, u32 topColor) {
    const glm::vec3 nw(x0, c.h[0] * kHeightStep, z0), ne(x0 + 1, c.h[1] * kHeightStep, z0);
    const glm::vec3 se(x0 + 1, c.h[2] * kHeightStep, z0 + 1), sw(x0, c.h[3] * kHeightStep, z0 + 1);
    const auto top = [&](glm::vec3 p) { return glm::vec3(p.x, topY, p.z); };
    mb.triangleUp(top(nw), top(se), top(ne), topColor);
    mb.triangleUp(top(nw), top(sw), top(se), topColor);
    const auto side = [&](glm::vec3 a, glm::vec3 b, glm::vec3 dir) {
        if (a.y >= topY - 1e-4f && b.y >= topY - 1e-4f) return;
        mb.quadFacing(a, b, top(b), top(a), kFoundation, dir);
    };
    side(nw, ne, {0, 0, -1});
    side(ne, se, {1, 0, 0});
    side(se, sw, {0, 0, 1});
    side(sw, nw, {-1, 0, 0});
}

} // namespace

ChunkGeometry buildChunkGeometry(const Chunk& chunk, const TerrainGenerator& gen, const WaterLookup& waterOutside,
                                 int lod, const World* world, const std::function<bool(u32)>& residential,
                                 const TileLookup& tilesOutside, float cover) {
    ChunkGeometry out;
    const TilePos origin = chunkOrigin(chunk.pos);
    const u64 seed = gen.seed();
    const i32 step = 1 << lod;
    const i32 cells = kChunkSize / step;

    // Water flags for the chunk plus a one tile border ring.
    constexpr i32 kRing = kChunkSize + 2;
    std::array<u8, kRing * kRing> water{};
    for (i32 ly = -1; ly <= kChunkSize; ++ly) {
        for (i32 lx = -1; lx <= kChunkSize; ++lx) {
            const bool inside = lx >= 0 && lx < kChunkSize && ly >= 0 && ly < kChunkSize;
            const Tile* t = inside ? &chunk.tile(lx, ly) : nullptr;
            const bool w = t ? t->type == TileType::Water || waterUnderBridge(*t) || waterUnderBuilding(*t)
                             : waterOutside(TilePos{origin.x + lx, origin.y + ly});
            water[static_cast<usize>((ly + 1) * kRing + lx + 1)] = w ? 1 : 0;
        }
    }
    const auto isWater = [&](i32 lx, i32 ly) { return water[static_cast<usize>((ly + 1) * kRing + lx + 1)] != 0; };

    // Colours of open ground on the chunk's tiles and the ring around it (0 elsewhere). Each
    // corner takes the mean of the open tiles around it, so grass, rock, sand and snow shade into
    // each other rather than changing tile by tile, alike on both sides of a chunk border.
    std::array<u32, kRing * kRing> openColor{};
    {
        constexpr i32 kRingCorners = kRing + 1;
        std::array<i64, kRingCorners * kRingCorners> raw{};
        std::array<bool, kRingCorners * kRingCorners> known{};
        const auto rawAt = [&](i32 lx, i32 ly) {
            const usize k = static_cast<usize>((ly + 1) * kRingCorners + lx + 1);
            if (!known[k]) {
                raw[k] = gen.rawHeight(origin.x + lx, origin.y + ly);
                known[k] = true;
            }
            return raw[k];
        };
        for (i32 ly = -1; ly <= kChunkSize; ++ly) {
            for (i32 lx = -1; lx <= kChunkSize; ++lx) {
                const bool inside = lx >= 0 && lx < kChunkSize && ly >= 0 && ly < kChunkSize;
                const TilePos p{origin.x + lx, origin.y + ly};
                Tile generated;
                const Tile* t = inside ? &chunk.tile(lx, ly) : tilesOutside ? tilesOutside(p) : world ? world->map.tile(p) : nullptr;
                if (!t) {
                    const i64 r[4] = {rawAt(lx, ly), rawAt(lx + 1, ly), rawAt(lx + 1, ly + 1), rawAt(lx, ly + 1)};
                    u16 f[4];
                    for (int c = 0; c < 4; ++c)
                        f[c] = gen.smooth() ? TerrainGenerator::quantiseFine(r[c]) : fineOf(TerrainGenerator::quantise(r[c]));
                    generated = gen.generateTile(p, r, f);
                    t = &generated;
                }
                openColor[static_cast<usize>((ly + 1) * kRing + lx + 1)] = openGround(*t) ? groundColor(*t) : 0;
            }
        }
    }
    // Blended colour at a corner of the tile grid, with broad, gentle variation over the land:
    // lighter and darker stretches, warmer and cooler ones. 0 where no open ground meets.
    const auto openCorner = [&](i32 lx, i32 ly) -> u32 {
        glm::vec3 sum(0.0f);
        int n = 0;
        for (i32 dy = -1; dy <= 0; ++dy)
            for (i32 dx = -1; dx <= 0; ++dx)
                if (const u32 c = openColor[static_cast<usize>((ly + dy + 1) * kRing + lx + dx + 1)]) {
                    sum += unpackRgb(c);
                    ++n;
                }
        if (n == 0) return 0;
        const i32 wx = origin.x + lx, wy = origin.y + ly;
        const float light = 0.95f + 0.1f * softNoise(seed, 0x7E50, wx, wy, 24);
        const float warm = softNoise(seed, 0x7E51, wx, wy, 48) - 0.5f;
        return rgbaf(sum / static_cast<float>(n) * light * glm::vec3(1.0f + warm * 0.08f, 1.0f, 1.0f - warm * 0.1f));
    };

    // Visual corner heights on the (possibly coarse) grid; sea floor corners are lowered.
    const i32 gridN = cells + 1;
    std::vector<float> cornerY(static_cast<usize>(gridN * gridN));
    out.minY = 1e9f;
    out.maxY = -1e9f;
    for (i32 j = 0; j < gridN; ++j) {
        for (i32 i = 0; i < gridN; ++i) {
            const i32 lx = i * step, ly = j * step;
            const u16 h = chunk.fine(lx, ly);
            float y = static_cast<float>(h) * (kHeightStep / kSubLevels);
            if (h == 0 && isWater(lx - 1, ly - 1) && isWater(lx, ly - 1) && isWater(lx - 1, ly) && isWater(lx, ly))
                y = seabedY(gen.rawHeight(origin.x + lx, origin.y + ly));
            cornerY[static_cast<usize>(j * gridN + i)] = y;
            out.minY = std::min(out.minY, y);
            out.maxY = std::max(out.maxY, y);
        }
    }
    out.minY -= 2.5f; // skirts
    out.maxY += kHeightStep;
    const auto cy = [&](i32 i, i32 j) { return cornerY[static_cast<usize>(j * gridN + i)]; };
    const float fs = static_cast<float>(step);
    std::vector<u32> cornerColor(static_cast<usize>(gridN * gridN));
    for (i32 j = 0; j < gridN; ++j)
        for (i32 i = 0; i < gridN; ++i) cornerColor[static_cast<usize>(j * gridN + i)] = openCorner(i * step, j * step);

    MeshBuilder ground;
    ground.vertices.reserve(static_cast<usize>(cells * cells) * 6 + static_cast<usize>(cells) * 24);
    MeshBuilder waterMesh;
    std::vector<u32> cellColor(static_cast<usize>(cells * cells));
    for (i32 j = 0; j < cells; ++j) {
        for (i32 i = 0; i < cells; ++i) {
            const i32 lx = i * step, ly = j * step;
            const i32 sx = lx + step / 2, sy = ly + step / 2;
            const Tile& tile = chunk.tile(sx, sy);
            const TilePos wp{origin.x + lx, origin.y + ly};
            const float x0 = static_cast<float>(lx), x1 = x0 + fs;
            const float z0 = static_cast<float>(ly), z1 = z0 + fs;
            const glm::vec3 nw(x0, cy(i, j), z0), ne(x1, cy(i + 1, j), z0);
            const glm::vec3 se(x1, cy(i + 1, j + 1), z1), sw(x0, cy(i, j + 1), z1);

            u32 base = groundColor(tile);
            if (tile.type == TileType::Water || waterUnderBuilding(tile)) {
                const float depth = std::min((nw.y + ne.y + se.y + sw.y) * 0.25f, 0.0f);
                base = shade(base, std::max(0.45f, 1.0f + depth * 0.18f));
            } else if (tile.type == TileType::Footpath) {
                base = stationStyle::mortar;
            } else if (tile.type == TileType::Building) {
                base = lod == 0 ? (residential ? (residential(tile.index) ? kGarden : kPlaza) : buildingGround(tile, world)) : kUrbanBuilding;
            } else if ((tile.type == TileType::Road || tile.type == TileType::Station) && lod > 0) {
                base = kUrbanRoad;
            } else if (tile.type == TileType::Depot) {
                base = lod == 0 ? kPlaza : kUrbanBuilding;
            } else if (tile.type == TileType::Rail && lod > 0) {
                base = kBallast;
            } else if (tile.type == TileType::Station && static_cast<StopKind>(tile.extra) == StopKind::Dock) {
                base = lod == 0 ? kPlaza : kUrbanBuilding;
            }
            cellColor[static_cast<usize>(j * cells + i)] = base;

            // Structures on slopes stand on a flat foundation instead of the terrain.
            TopSurface top;
            bool structure = false;
            const bool roadLike =
                tile.type == TileType::Road ||
                (tile.type == TileType::Station && (static_cast<StopKind>(tile.extra) == StopKind::Bus ||
                                                    static_cast<StopKind>(tile.extra) == StopKind::Tram));
            const bool railStation =
                tile.type == TileType::Station && static_cast<StopKind>(tile.extra) == StopKind::Rail;
            const bool rail = tile.type == TileType::Rail;
            const bool dock = tile.type == TileType::Station && static_cast<StopKind>(tile.extra) == StopKind::Dock;
            const bool freeDepot = tile.type == TileType::Depot && (tile.extra2 & kFreeTrackDepot);
            const bool building = tile.type == TileType::Building && !waterUnderBuilding(tile);
            if (lod == 0 && !freeDepot && (roadLike || rail || railStation || dock || building || tile.type == TileType::Depot)) {
                structure = true;
                top.corners = chunk.corners(lx, ly);
                const Slope slope = computeSlope(top.corners);
                top.surface = roadLike ? roadSurface(slope, allRoadPieces(tile))
                              : rail   ? railSurface(slope, tile.rail)
                                       : buildingSurface(slope);
                if (top.surface == Surface::Invalid) top.surface = Surface::Foundation;
                top.level = flatSurfaceLevel(slope, top.surface);
                if (tile.span) {
                    top.surface = Surface::Flat;
                    top.level = tile.deckHeight;
                    out.maxY = std::max(out.maxY, top.level * kHeightStep + 0.8f);
                }
            }

            if (structure && top.surface == Surface::Foundation) {
                addFoundation(ground, top.corners, x0, z0, static_cast<float>(top.level) * kHeightStep, base);
            } else {
                const u32 h = hashCoords(seed, wp.x, wp.y, 0x7E44);
                const FineCorners coarse{{chunk.fine(lx, ly), chunk.fine(lx + step, ly), chunk.fine(lx + step, ly + step),
                                          chunk.fine(lx, ly + step)}};
                if (openGround(tile)) {
                    const auto at = [&](i32 ci, i32 cj, float f) {
                        const u32 c = cornerColor[static_cast<usize>(cj * gridN + ci)];
                        return shade(c ? c : base, f);
                    };
                    const float f0 = softJitter(h), f1 = softJitter(h >> 8);
                    if (!coarse.splitNESW()) {
                        ground.triangleUp(nw, se, ne, at(i, j, f0), at(i + 1, j + 1, f0), at(i + 1, j, f0));
                        ground.triangleUp(nw, sw, se, at(i, j, f1), at(i, j + 1, f1), at(i + 1, j + 1, f1));
                    } else {
                        ground.triangleUp(nw, sw, ne, at(i, j, f0), at(i, j + 1, f0), at(i + 1, j, f0));
                        ground.triangleUp(ne, sw, se, at(i + 1, j, f1), at(i, j + 1, f1), at(i + 1, j + 1, f1));
                    }
                } else {
                    const u32 c0 = shade(base, jitter(h));
                    const u32 c1 = shade(base, jitter(h >> 8));
                    if (!coarse.splitNESW()) {
                        ground.triangleUp(nw, se, ne, c0);
                        ground.triangleUp(nw, sw, se, c1);
                    } else {
                        ground.triangleUp(nw, sw, ne, c0);
                        ground.triangleUp(ne, sw, se, c1);
                    }
                }
            }
            if (structure && (tile.span & 3) == kSpanBridge) {
                const float y = tile.deckHeight * kHeightStep;
                ground.box({x0, y - 0.12f, z0}, {x0 + 1.0f, y, z0 + 1.0f}, kFoundation);
                const bool axisX = tile.span & kSpanAxisX;
                for (const float side : {0.07f, 0.93f}) {
                    if (axisX) ground.box({x0, y, z0 + side - 0.025f}, {x1, y + 0.17f, z0 + side + 0.025f}, kSteel);
                    else ground.box({x0 + side - 0.025f, y, z0}, {x0 + side + 0.025f, y + 0.17f, z1}, kSteel);
                }
                if (!waterUnderBridge(tile) && (tile.index & 0xFFFF) % 4 == 2) {
                    const float bottom = std::min({nw.y, ne.y, se.y, sw.y});
                    ground.box({x0 + 0.4f, bottom, z0 + 0.4f}, {x0 + 0.6f, y - 0.12f, z0 + 0.6f}, kFoundation);
                }
            }
            if (structure && (tile.span & 3) == kSpanTunnel && spanPortal(tile)) {
                const float y = tile.deckHeight * kHeightStep;
                const bool axisX = tile.span & kSpanAxisX;
                for (const float side : {0.08f, 0.82f}) {
                    if (axisX) ground.box({x0 + 0.4f, y, z0 + side}, {x0 + 0.6f, y + 0.7f, z0 + side + 0.1f}, kFoundation);
                    else ground.box({x0 + side, y, z0 + 0.4f}, {x0 + side + 0.1f, y + 0.7f, z0 + 0.6f}, kFoundation);
                }
                if (axisX) ground.box({x0 + 0.4f, y + 0.6f, z0 + 0.08f}, {x0 + 0.6f, y + 0.75f, z0 + 0.92f}, kFoundation);
                else ground.box({x0 + 0.08f, y + 0.6f, z0 + 0.4f}, {x0 + 0.92f, y + 0.75f, z0 + 0.6f}, kFoundation);
            }
            if (lod == 0 && tile.type == TileType::Footpath) {
                const auto fine = chunk.fineCorners(lx, ly);
                const auto pave = [&](float u0, float v0, float u1, float v1, float lift, u32 colour) {
                    const auto p = [&](float u, float v) {
                        return glm::vec3(x0 + u, tileSurfaceLevel(fine, u, v) * kHeightStep + lift, z0 + v);
                    };
                    ground.triangleUp(p(u0, v0), p(u1, v1), p(u1, v0), colour);
                    ground.triangleUp(p(u0, v0), p(u0, v1), p(u1, v1), colour);
                };
                // Warm stone slabs echo the platforms, with dark joints and contrasting kerbs.
                for (int row = 0; row < 4; ++row)
                    for (int col = 0; col < 4; ++col) {
                        const float u = col * 0.25f, v = row * 0.25f;
                        pave(u + 0.007f, v + 0.007f, u + 0.243f, v + 0.243f, 0.012f,
                             shade(stationStyle::paving, ((wp.x + wp.y + row + col) & 1) ? 1.0f : 0.97f));
                    }
                const auto connected = [&](int dx, int dz) {
                    const Tile* neighbour = nullptr;
                    if (lx + dx >= 0 && lx + dx < kChunkSize && ly + dz >= 0 && ly + dz < kChunkSize)
                        neighbour = &chunk.tile(lx + dx, ly + dz);
                    else if (world)
                        neighbour = world->map.tile({wp.x + dx, wp.y + dz});
                    return !neighbour || neighbour->type == TileType::Footpath ||
                           neighbour->type == TileType::Road || neighbour->type == TileType::Rail ||
                           neighbour->type == TileType::Station;
                };
                if (!connected(-1, 0)) pave(0, 0, 0.045f, 1, 0.027f, stationStyle::stone);
                if (!connected(1, 0)) pave(0.955f, 0, 1, 1, 0.027f, stationStyle::stone);
                if (!connected(0, -1)) pave(0, 0, 1, 0.045f, 0.027f, stationStyle::stone);
                if (!connected(0, 1)) pave(0, 0.955f, 1, 1, 0.027f, stationStyle::stone);
                // A small pedestrian stencil distinguishes the approach from a service road.
                if ((wp.x + wp.y) % 3 == 0) {
                    const auto mark = [&](float a, float b, float c, float d) {
                        pave(a, b, c, d, 0.018f, stationStyle::lettering);
                    };
                    mark(0.47f, 0.28f, 0.53f, 0.34f);
                    mark(0.475f, 0.36f, 0.525f, 0.53f);
                    mark(0.40f, 0.39f, 0.60f, 0.425f);
                    mark(0.45f, 0.51f, 0.485f, 0.67f);
                    mark(0.515f, 0.51f, 0.55f, 0.67f);
                }
            }
            if (structure && roadLike)
                addRoad(ground, top, x0, z0, roadPieces(tile), tramPieces(tile), tile.type == TileType::Station);
            if (structure && tile.type == TileType::Depot) {
                quadOnSurface(ground, top, x0, z0, 0.04f, 0.04f, 0.96f, 0.96f, 0.012f, kPlaza);
                if (tile.rail) addRail(ground, top, x0, z0, tile.rail, static_cast<Dir>(tile.extra));
            }
            if (structure && (rail || (roadLike && tile.rail))) addRail(ground, top, x0, z0, tile.rail);
            if (structure && railStation) {
                addRail(ground, top, x0, z0, tile.rail);
                addPlatforms(ground, x0, z0, static_cast<float>(top.level) * kHeightStep, tile.rail);
            }

            if (tile.type == TileType::Water || waterUnderBridge(tile) || waterUnderBuilding(tile)) {
                const u32 wc = rgba(255, 255, 255);
                waterMesh.triangleUp({x0, kWaterLevel, z0}, {x1, kWaterLevel, z1}, {x1, kWaterLevel, z0}, wc);
                waterMesh.triangleUp({x0, kWaterLevel, z0}, {x0, kWaterLevel, z1}, {x1, kWaterLevel, z1}, wc);
            }

            if (lod == 0 && tile.type == TileType::Clear && tile.trees > 0) {
                const FineCorners fineCorners = chunk.fineCorners(lx, ly);
                for (u32 t = 0; t < tile.trees; ++t) {
                    const u32 th = hashCoords(seed, wp.x, wp.y, 0x7EE0 + t);
                    const float r1 = static_cast<float>(th & 0xFF) / 255.0f;
                    const float r2 = static_cast<float>((th >> 8) & 0xFF) / 255.0f;
                    float fx, fz;
                    if (tile.trees == 1) {
                        fx = 0.25f + r1 * 0.5f;
                        fz = 0.25f + r2 * 0.5f;
                    } else {
                        fx = static_cast<float>(t % 2) * 0.5f + 0.1f + r1 * 0.3f;
                        fz = static_cast<float>(t / 2) * 0.5f + 0.1f + r2 * 0.3f;
                    }
                    TreePlacement tp;
                    tp.pos = glm::vec3(x0 + fx, tileSurfaceLevel(fineCorners, fx, fz) * kHeightStep, z0 + fz);
                    tp.scale = 0.75f + static_cast<float>((th >> 16) & 0xFF) / 255.0f * 0.5f;
                    tp.yaw = static_cast<float>((th >> 24) & 0xFF) / 255.0f * glm::two_pi<float>();
                    tp.type = tile.treeType;
                    tp.variant = static_cast<u8>((th >> 4) % 3);
                    tp.snowy = tile.ground == Ground::Snow;
                    out.trees.push_back(tp);
                    out.maxY = std::max(out.maxY, tp.pos.y + 2.0f);
                }
            }

            if (cover > 0.0f && lod == 0 && tile.type == TileType::Clear && !tile.span)
                scatterCover(out.cover, tile, wp, x0, z0, chunk.fineCorners(lx, ly), seed, cover,
                             isWater(lx - 1, ly) || isWater(lx + 1, ly) || isWater(lx, ly - 1) || isWater(lx, ly + 1),
                             {cornerColor[static_cast<usize>(j * gridN + i)], cornerColor[static_cast<usize>(j * gridN + i + 1)],
                              cornerColor[static_cast<usize>((j + 1) * gridN + i + 1)], cornerColor[static_cast<usize>((j + 1) * gridN + i)]},
                             base);
        }
    }

    // Skirts hide cracks between neighbouring chunks rendered at different levels of detail.
    constexpr float kSkirt = 2.5f;
    const float edge = static_cast<float>(kChunkSize);
    const glm::vec3 down(0, -kSkirt, 0);
    for (i32 k = 0; k < cells; ++k) {
        const float a = static_cast<float>(k * step), b = a + fs;
        const u32 cn = shade(cellColor[static_cast<usize>(k)], 0.8f);
        const u32 cs = shade(cellColor[static_cast<usize>((cells - 1) * cells + k)], 0.8f);
        const u32 cw = shade(cellColor[static_cast<usize>(k * cells)], 0.8f);
        const u32 ce = shade(cellColor[static_cast<usize>(k * cells + cells - 1)], 0.8f);
        const glm::vec3 n0(a, cy(k, 0), 0), n1(b, cy(k + 1, 0), 0);
        ground.quadFacing(n0, n1, n1 + down, n0 + down, cn, {0, 0, -1});
        const glm::vec3 s0(a, cy(k, cells), edge), s1(b, cy(k + 1, cells), edge);
        ground.quadFacing(s0, s1, s1 + down, s0 + down, cs, {0, 0, 1});
        const glm::vec3 w0(0, cy(0, k), a), w1(0, cy(0, k + 1), b);
        ground.quadFacing(w0, w1, w1 + down, w0 + down, cw, {-1, 0, 0});
        const glm::vec3 e0(edge, cy(cells, k), a), e1(edge, cy(cells, k + 1), b);
        ground.quadFacing(e0, e1, e1 + down, e0 + down, ce, {1, 0, 0});
    }

    out.ground = std::move(ground.vertices);
    out.water = std::move(waterMesh.vertices);
    return out;
}

} // namespace tp::gfx
