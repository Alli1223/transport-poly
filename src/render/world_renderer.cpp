#include "render/world_renderer.hpp"

#include "core/hash.hpp"
#include "core/log.hpp"
#include "render/shaders.hpp"
#include "render/picking.hpp"
#include <future>
#include <chrono>
#include <map>
#include <set>
#include "render/vehicle_pose.hpp"
#include "render/catenary.hpp"
#include "render/person_pose.hpp"
#include "sim/industries.hpp"
#include "sim/rail.hpp"
#include "sim/road.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace tp::gfx {

namespace {

glm::vec3 lin(const glm::vec3& c) { return glm::pow(c, glm::vec3(2.2f)); }

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Frustum {
    glm::vec4 planes[6];

    bool intersects(const glm::vec3& mn, const glm::vec3& mx) const {
        for (const glm::vec4& p : planes) {
            const glm::vec3 v(p.x > 0 ? mx.x : mn.x, p.y > 0 ? mx.y : mn.y, p.z > 0 ? mx.z : mn.z);
            if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0.0f) return false;
        }
        return true;
    }
};

Frustum extractFrustum(const glm::mat4& m) {
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    return Frustum{{r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2}};
}

bool tileIsWater(const Map& map, TilePos t) {
    if (const Tile* tile = map.tile(t)) return tile->type == TileType::Water || waterUnderBridge(*tile) || waterUnderBuilding(*tile);
    const TerrainGenerator& g = map.generator();
    return g.rawHeight(t.x, t.y) < 0 && g.rawHeight(t.x + 1, t.y) < 0 && g.rawHeight(t.x + 1, t.y + 1) < 0 &&
           g.rawHeight(t.x, t.y + 1) < 0;
}

ModelId coverModel(const CoverPlacement& c) {
    switch (c.kind) {
    case Cover::Grass: return static_cast<ModelId>(models::kGrass0 + c.variant % 3);
    case Cover::Flowers: return static_cast<ModelId>(models::kFlowers0 + c.variant % 4);
    case Cover::Fern: return static_cast<ModelId>(models::kFern0 + c.variant % 2);
    case Cover::Reeds: return static_cast<ModelId>(models::kReeds0 + c.variant % 2);
    case Cover::Heather: return static_cast<ModelId>(models::kHeather0 + c.variant % 2);
    case Cover::Pebbles: return static_cast<ModelId>(models::kPebbles0 + c.variant % 2);
    }
    return models::kGrass0;
}

// Ground cover grows on chunks this close to the camera's target (tiles), as densely as the
// setting asks; it fades out between these distances from the eye.
constexpr double coverRadius(int detail) { return detail >= 2 ? 56.0 : 40.0; }
constexpr float coverDensity(int detail) { return detail >= 2 ? 1.0f : detail == 1 ? 0.5f : 0.0f; }
constexpr float kCoverFadeStart = 26.0f, kCoverFadeEnd = 50.0f;

ModelId treeModel(const TreePlacement& t) {
    switch (t.type) {
    case TreeType::Palm: return static_cast<ModelId>(models::kTreePalm0 + t.variant % 2);
    case TreeType::Cactus: return static_cast<ModelId>(models::kTreeCactus0 + t.variant % 2);
    case TreeType::Acacia: return static_cast<ModelId>(models::kTreeAcacia0 + t.variant % 2);
    case TreeType::Shrub: return static_cast<ModelId>(models::kShrub0 + t.variant % 2);
    case TreeType::Willow: return static_cast<ModelId>(models::kTreeWillow0 + t.variant % 2);
    case TreeType::Bush: return static_cast<ModelId>(models::kBush0 + t.variant % 2);
    case TreeType::Birch: return static_cast<ModelId>(models::kTreeBirch0 + t.variant % 3);
    case TreeType::Pine: return t.snowy ? models::kTreeConiferSnow : static_cast<ModelId>(models::kTreePine0 + t.variant % 2);
    default: break;
    }
    if (t.snowy) return models::kTreeConiferSnow;
    const ModelId base = t.type == TreeType::Conifer ? models::kTreeConifer0 : models::kTreeDeciduous0;
    return static_cast<ModelId>(base + t.variant % 3);
}

// Where the street lamp of a town road tile stands (x, height, z), if it has one: at the north-west
// corner of every fourth tile on the grid, and at the kerb of every third tile of a curved road.
std::optional<glm::dvec3> streetLamp(const World& world, TilePos t) {
    const Tile* tile = world.map.tile(t);
    if (!tile || tile->type != TileType::Road || tile->roadOwner != kOwnerTown || tile->span || tile->rail) return std::nullopt;
    const auto it = world.roadCurveTiles.find(ChunkPos{t.x, t.y}.key());
    if (it == world.roadCurveTiles.end()) {
        if (((static_cast<u32>(t.x) + static_cast<u32>(t.y)) & 3u) != 0) return std::nullopt;
        return glm::dvec3(t.x + 0.08, roadSurfaceY(world, t, 0.08f, 0.08f) + 0.02, t.y + 0.08);
    }
    const RoadCurve* c = world.roadCurves.tryGet(it->second.curve);
    const usize i = it->second.index;
    if (!c || i % 3 != 1 || i + 1 >= c->tileStart.size()) return std::nullopt;
    TDir d;
    const TPoint p = roadcurves::pointAt(*c, (c->tileStart[i] + c->tileStart[i + 1]) / 2, &d);
    const double x = p.x / double(kTrackUnits) + d.z / double(kDirOne) * 0.45;
    const double z = p.z / double(kTrackUnits) - d.x / double(kDirOne) * 0.45;
    const float y = roadSurfaceY(world, t, std::clamp(static_cast<float>(x - t.x), 0.0f, 1.0f),
                                 std::clamp(static_cast<float>(z - t.y), 0.0f, 1.0f));
    return glm::dvec3(x, y + 0.02, z);
}

float facingYaw(Dir d) {
    switch (d) {
    case Dir::S: return 0.0f;
    case Dir::E: return glm::half_pi<float>();
    case Dir::N: return glm::pi<float>();
    case Dir::W: return -glm::half_pi<float>();
    }
    return 0.0f;
}

u32 buildingTint(const Building& b) {
    static constexpr u32 kHomes[] = {rgba(232, 222, 196), rgba(240, 238, 232), rgba(184, 104, 80), rgba(214, 192, 144),
                                     rgba(236, 222, 156), rgba(198, 214, 228), rgba(228, 194, 188), rgba(206, 204, 198)};
    static constexpr u32 kCommercial[] = {rgba(214, 212, 206), rgba(222, 210, 186), rgba(236, 234, 228),
                                          rgba(200, 196, 188)};
    static constexpr u32 kIndustrial[] = {rgba(166, 98, 74), rgba(172, 172, 166), rgba(152, 164, 178)};
    const u32 h = mix32(b.serial * 2654435761u);
    switch (b.info().category) {
    case BuildingCategory::Residential: return kHomes[h % std::size(kHomes)];
    case BuildingCategory::Industrial: return kIndustrial[h % std::size(kIndustrial)];
    default: return kCommercial[h % std::size(kCommercial)];
    }
}

u32 companyTint(const World& w, CompanyId id) {
    const Company* c = w.companies.tryGet(id);
    const u32 rgb = companyColourRgb(c ? c->colour : 14);
    return rgba(static_cast<u8>(rgb >> 16), static_cast<u8>(rgb >> 8), static_cast<u8>(rgb));
}

ModelId vehicleModelId(const VehicleModel& m) {
    switch (m.type) {
    case VehicleType::Road: return models::bus(m.visual);
    case VehicleType::Tram: return models::tram(m.visual);
    case VehicleType::Rail: return models::train(m.visual);
    case VehicleType::Ship: return models::ship(m.visual);
    }
    return models::bus(0);
}

} // namespace

namespace {

// C++ twins of the shaders' weather noise, to place the clouds where the shadows fall.
float hash12(glm::vec2 p) {
    glm::vec3 p3 = glm::fract(glm::vec3(p.x, p.y, p.x) * 0.1031f);
    p3 += glm::dot(p3, glm::vec3(p3.y, p3.z, p3.x) + 33.33f);
    return glm::fract((p3.x + p3.y) * p3.z);
}

float weatherNoise(glm::vec2 p) {
    const glm::vec2 i = glm::floor(p), f = p - i;
    const glm::vec2 u = f * f * (3.0f - 2.0f * f);
    return glm::mix(glm::mix(hash12(i), hash12(i + glm::vec2(1, 0)), u.x), glm::mix(hash12(i + glm::vec2(0, 1)), hash12(i + glm::vec2(1, 1)), u.x), u.y);
}

float cloudField(glm::vec2 world, glm::vec2 drift, float cover) {
    const glm::vec2 p = (world + drift) / 90.0f;
    const float n = weatherNoise(p) * 0.55f + weatherNoise(p * 2.13f + 7.3f) * 0.3f + weatherNoise(p * 4.37f + 1.7f) * 0.15f;
    const float edge = 1.0f - cover * 0.75f;
    return glm::smoothstep(edge, edge + 0.16f, n);
}

// Smooth noise over time (days), in [0, 1].
float timeNoise(u64 seed, u32 salt, double t) {
    const double i = std::floor(t);
    const float f = static_cast<float>(t - i);
    const auto at = [&](double k) {
        return static_cast<float>(hashCoords(seed, static_cast<i32>(static_cast<i64>(k)), 0, salt) >> 8) / 16777216.0f;
    };
    const float u = f * f * (3.0f - 2.0f * f);
    return glm::mix(at(i), at(i + 1.0), u);
}

constexpr float kCloudHeight = 46.0f;

} // namespace

Weather computeWeather(u64 seed, double gameDays, double minuteOfDay) {
    Weather w;
    // Cloud cover drifts over a few days between clear spells and grey ones.
    const float c = timeNoise(seed, 0xC10D, gameDays / 1.6) * 0.65f + timeNoise(seed, 0xC10E, gameDays / 0.45) * 0.35f;
    w.cover = glm::clamp((c - 0.22f) * 1.5f, 0.0f, 1.0f);
    // Mist on humid mornings, thinning as the sun climbs; a little in the evening too.
    const float humid = timeNoise(seed, 0x0157, gameDays / 1.1);
    const float hour = static_cast<float>(std::fmod(minuteOfDay, 1440.0) / 60.0);
    const float morning = glm::smoothstep(3.5f, 6.0f, hour) * (1.0f - glm::smoothstep(7.0f, 10.5f, hour));
    const float evening = glm::smoothstep(19.0f, 22.0f, hour) * 0.35f + (1.0f - glm::smoothstep(1.0f, 4.0f, hour)) * 0.35f;
    w.mist = glm::clamp((humid - 0.35f) * 1.6f, 0.0f, 1.0f) * std::max(morning, evening) * 0.6f + w.cover * 0.06f;
    const float angle = timeNoise(seed, 0x3171, gameDays / 4.0) * 6.2831853f * 2.0f;
    w.wind = glm::vec2(std::cos(angle), std::sin(angle));
    return w;
}

SkyState computeSky(double minuteOfDay) {
    SkyState s;
    const float dayFrac = static_cast<float>(std::fmod(minuteOfDay, 1440.0) / 1440.0);
    const float a = (dayFrac - 0.25f) * 6.2831853f; // 0 at 06:00, pi/2 at noon
    const float elev = std::sin(a);
    s.sunDir = glm::normalize(glm::vec3(std::cos(a), std::max(elev, -0.3f) * 0.95f + 0.02f, 0.42f));
    const float day = smooth(-0.15f, 0.25f, elev);
    s.night = 1.0f - day;
    s.sunStrength = smooth(-0.04f, 0.12f, elev);
    const float warm = 1.0f - smooth(0.0f, 0.45f, elev);
    s.sunColor = lin(glm::mix(glm::vec3(1.0f, 0.93f, 0.82f), glm::vec3(1.0f, 0.55f, 0.32f), warm)) * 1.15f;
    s.zenith = lin(glm::mix(glm::vec3(0.02f, 0.03f, 0.09f), glm::vec3(0.27f, 0.50f, 0.86f), day));
    glm::vec3 horizon = glm::mix(glm::vec3(0.05f, 0.07f, 0.14f), glm::vec3(0.70f, 0.82f, 0.93f), day);
    const float sunset = smooth(-0.2f, 0.05f, elev) * (1.0f - smooth(0.05f, 0.35f, elev));
    horizon = glm::mix(horizon, glm::vec3(0.96f, 0.58f, 0.38f), sunset * 0.65f);
    s.horizon = lin(horizon);
    s.ambientSky = lin(glm::mix(glm::vec3(0.10f, 0.12f, 0.22f), glm::vec3(0.50f, 0.56f, 0.66f), day));
    s.ambientGround = lin(glm::mix(glm::vec3(0.05f, 0.05f, 0.08f), glm::vec3(0.34f, 0.31f, 0.26f), day));
    return s;
}

bool WorldRenderer::init() {
    terrainShader_ = Shader::create("terrain", shaders::terrainVS(), shaders::terrainFS());
    modelShader_ = Shader::create("model", shaders::modelVS(), shaders::modelFS());
    waterShader_ = Shader::create("water", shaders::waterVS(), shaders::waterFS());
    skyShader_ = Shader::create("sky", shaders::skyVS(), shaders::skyFS());
    cloudShader_ = Shader::create("cloud", shaders::cloudVS(), shaders::cloudFS());
    glowShader_ = Shader::create("glow", shaders::glowVS(), shaders::glowFS());
    shadowStaticShader_ = Shader::create("shadow-static", shaders::shadowStaticVS(), shaders::shadowFS());
    shadowModelShader_ = Shader::create("shadow-model", shaders::shadowModelVS(), shaders::shadowFS());
    overlayShader_ = Shader::create("overlay", shaders::overlayVS(), shaders::overlayFS());
    if (!terrainShader_ || !modelShader_ || !waterShader_ || !skyShader_ || !shadowStaticShader_ ||
        !shadowModelShader_ || !overlayShader_ || !cloudShader_ || !glowShader_)
        return false;

    for (const Shader* s : {&*terrainShader_, &*modelShader_, &*waterShader_}) {
        s->use();
        s->set("uShadowMap", 1);
    }
    glUseProgram(0);

    models_.build();
    frameUbo_ = makeBuffer();
    glBindBuffer(GL_UNIFORM_BUFFER, frameUbo_.get());
    glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameUniformBinding, frameUbo_.get());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    emptyVao_ = makeVertexArray();
    // Glow quads: position, offset across the halo (-1..1) and colour with strength.
    glowVao_ = makeVertexArray();
    glowVbo_ = makeBuffer();
    glBindVertexArray(glowVao_.get());
    glBindBuffer(GL_ARRAY_BUFFER, glowVbo_.get());
    constexpr GLsizei kGlowStride = 9 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kGlowStride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kGlowStride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kGlowStride,
                          reinterpret_cast<const void*>(5 * sizeof(float)));
    glBindVertexArray(0);
    if (!fx_.init()) return false;
    ensureShadowTarget();
    return true;
}

void WorldRenderer::collectLamps(const World& world, const glm::dvec3& origin, const glm::dvec3& focus,
                                 double radius) {
    const float night = sky_.night;
    if (night < 0.02f) return;
    const glm::vec3 warm(1.0f, 0.78f, 0.48f), cool(0.92f, 0.95f, 1.0f);
    const auto rel = [&](double x, double y, double z) {
        return glm::vec3(static_cast<float>(x - origin.x), static_cast<float>(y),
                         static_cast<float>(z - origin.z));
    };
    // Platform lamps hang under the canopies of free-form stations, at every canopy post.
    const double reach = std::min(radius, 140.0);
    for (const auto [id, e] : world.trackEdges) {
        if (e.kind != TrackKind::Platform || e.samples.size() < 2) continue;
        const TrackSample& mid = e.samples[e.samples.size() / 2];
        const double mx = mid.p.x / double(kTrackUnits) - focus.x,
                     mz = mid.p.z / double(kTrackUnits) - focus.z;
        if (mx * mx + mz * mz > reach * reach) continue;
        const float length = static_cast<float>(e.length) / kTrackUnits;
        const int posts = std::max(2, static_cast<int>(length / 1.6f) + 1);
        for (int k = 0; k < posts; ++k) {
            const i32 at = static_cast<i32>(static_cast<i64>(e.length) * k / (posts - 1));
            const TrackSample a = track::pointOn(e, true, std::max(0, at - 16));
            const TrackSample b = track::pointOn(e, true, std::min(e.length, at + 16));
            const TrackSample c = track::pointOn(e, true, at);
            glm::vec2 t(static_cast<float>(b.p.x - a.p.x), static_cast<float>(b.p.z - a.p.z));
            t = glm::length(t) > 0.0f ? glm::normalize(t) : glm::vec2(1, 0);
            const glm::vec2 left(t.y, -t.x);
            const double cx = c.p.x / double(kTrackUnits), cz = c.p.z / double(kTrackUnits);
            const float y = trackRailY(c.h) + 0.017f + 0.42f;
            for (const float side : {-1.0f, 1.0f})
                glows_.push_back({rel(cx + left.x * side * 0.65, y, cz + left.y * side * 0.65), cool, 0.32f,
                                  night * 0.9f});
            lamps_.push_back({rel(cx, y, cz), 2.6f, cool * 0.9f * night, {0, -1, 0}, -2.0f, 3.0f});
        }
    }
    // Street lamps along town roads and lights in bus and tram shelters.
    const i32 fx = static_cast<i32>(std::floor(focus.x)), fz = static_cast<i32>(std::floor(focus.z));
    const i32 r = static_cast<i32>(std::min(radius, 40.0));
    for (i32 y = fz - r; y <= fz + r; ++y)
        for (i32 x = fx - r; x <= fx + r; ++x) {
            const Tile* t = world.map.tile({x, y});
            if (!t) continue;
            const TilePos tp{x, y};
            if (const auto lamp = streetLamp(world, tp)) {
                glows_.push_back({rel(lamp->x, lamp->y + 0.885, lamp->z), warm, 0.42f, night});
                lamps_.push_back({rel(lamp->x, lamp->y + 0.85, lamp->z),
                                  3.2f,
                                  warm * 0.85f * night,
                                  {0, -1, 0},
                                  -2.0f,
                                  0.0f});
            } else if (t->type == TileType::Station && (static_cast<StopKind>(t->extra) == StopKind::Bus ||
                                                        static_cast<StopKind>(t->extra) == StopKind::Tram)) {
                const float ground = roadSurfaceY(world, tp, 0.5f, 0.5f) + 0.015f;
                const bool northSouth = allRoadPieces(*t) & dirBit(Dir::N);
                for (const float side : {0.08f, 0.92f}) {
                    const glm::vec2 at = northSouth ? glm::vec2(side, 0.5f) : glm::vec2(0.5f, side);
                    glows_.push_back({rel(x + at.x, ground + 0.2f, y + at.y), cool, 0.22f, night * 0.8f});
                }
                lamps_.push_back({rel(x + 0.5, ground + 0.3f, y + 0.5),
                                  1.8f,
                                  cool * 0.6f * night,
                                  {0, -1, 0},
                                  -2.0f,
                                  1.0f});
            }
        }
}

void WorldRenderer::drawGlows(const glm::mat4& view) {
    if (glows_.empty()) return;
    // Billboards facing the camera.
    const glm::vec3 right(view[0][0], view[1][0], view[2][0]), up(view[0][1], view[1][1], view[2][1]);
    std::vector<float> v;
    const usize count = std::min<usize>(glows_.size(), 1200);
    v.reserve(count * 6 * 9);
    static constexpr float kQuad[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (usize i = 0; i < count; ++i) {
        const Glow& g = glows_[i];
        for (const auto& c : kQuad) {
            const glm::vec3 p = g.pos + (right * c[0] + up * c[1]) * g.size;
            v.insert(v.end(), {p.x, p.y, p.z, c[0], c[1], g.colour.r, g.colour.g, g.colour.b, g.strength});
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, glowVbo_.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(),
                 GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glowShader_->use();
    glBindVertexArray(glowVao_.get());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count * 6));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void WorldRenderer::ensureShadowTarget() {
    if (shadowFbo_ && shadowSize_ == settings.shadowMapSize) return;
    shadowSize_ = settings.shadowMapSize;
    shadowTex_ = makeTexture();
    glBindTexture(GL_TEXTURE_2D, shadowTex_.get());
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), shadowSize_, shadowSize_, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_BORDER));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_BORDER));
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, static_cast<GLint>(GL_COMPARE_REF_TO_TEXTURE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(GL_LEQUAL));
    glBindTexture(GL_TEXTURE_2D, 0);

    shadowFbo_ = makeFramebuffer();
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_.get(), 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::warn("Shadow framebuffer incomplete - disabling shadows");
        settings.shadows = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void WorldRenderer::buildChunkMesh(const World& world, ChunkPos pos, int lod, ChunkMesh& mesh, const ChunkGeometry* prepared,
                                   float cover) {
    const Map& map = world.map;
    const Chunk* active = map.chunk(pos);
    const Chunk* source = active;
    Chunk generated;
    if (!source && !prepared) {
        map.generator().generateChunk(pos, generated);
        source = &generated;
    }
    ChunkGeometry generatedGeo;
    if (!prepared) generatedGeo = buildChunkGeometry(
        *source, map.generator(), [&](TilePos t) { return tileIsWater(map, t); }, lod, &world, {}, {}, cover);
    const ChunkGeometry& geo = prepared ? *prepared : generatedGeo;
    mesh.pos = pos;
    mesh.lod = lod;
    mesh.ground.upload(geo.ground);
    mesh.water.upload(geo.water);
    mesh.minY = geo.minY;
    mesh.maxY = geo.maxY;

    std::vector<std::pair<ModelId, ModelInstance>> items;
    items.reserve(geo.trees.size());
    for (const TreePlacement& t : geo.trees) items.emplace_back(treeModel(t), makeInstance(t.pos, t.yaw, t.scale));
    mesh.trees.upload(items);
    items.clear();
    items.reserve(geo.cover.size());
    for (const CoverPlacement& c : geo.cover) items.emplace_back(coverModel(c), makeInstance(c.pos, c.yaw, c.scale, c.tint));
    mesh.cover.upload(items);
    mesh.coverDensity = cover;

    items.clear();
    if (active) {
        const TilePos o = chunkOrigin(pos);
        for (i32 ly = 0; ly < kChunkSize; ++ly) {
            for (i32 lx = 0; lx < kChunkSize; ++lx) {
                const Tile& t = active->tile(lx, ly);
                if (t.type != TileType::Building || t.extra != 0) continue;
                const Building* b = world.buildings.tryGet(BuildingId(t.index));
                if (!b) continue;
                const u8 size = b->size();
                // Surface level of the footprint (flat or on a foundation).
                const Slope s = map.slope(b->pos);
                const float y = static_cast<float>(flatSurfaceLevel(s, buildingSurface(s))) * kHeightStep;
                const glm::vec3 p(static_cast<float>(b->pos.x - o.x) + size * 0.5f, y,
                                  static_cast<float>(b->pos.y - o.y) + size * 0.5f);
                items.emplace_back(models::building(b->type, b->variant),
                                   makeInstance(p, facingYaw(b->facing), industries::isCountryside(b->type) ? static_cast<float>(size) / (b->info().size + 2) : 1.0f, buildingTint(*b)));
                mesh.maxY = std::max(mesh.maxY, y + 4.6f);
            }
        }
        // Stop shelters and depots.
        for (i32 ly = 0; ly < kChunkSize; ++ly) {
            for (i32 lx = 0; lx < kChunkSize; ++lx) {
                const Tile& t = active->tile(lx, ly);
                const TilePos wp{o.x + lx, o.y + ly};
                if (const auto lamp = streetLamp(world, wp)) {
                    const float y = static_cast<float>(lamp->y);
                    items.emplace_back(models::kStreetLamp, makeInstance({static_cast<float>(lamp->x - o.x), y,
                                                                          static_cast<float>(lamp->z - o.y)}, 0.0f, 1.0f));
                    mesh.maxY = std::max(mesh.maxY, y + 1.0f);
                }
                if (t.type == TileType::Station &&
                    (static_cast<StopKind>(t.extra) == StopKind::Bus || static_cast<StopKind>(t.extra) == StopKind::Tram)) {
                    const Station* st = world.stations.tryGet(StationId(t.index));
                    const u32 tint = companyTint(world, st ? st->owner : CompanyId{});
                    const float y = roadSurfaceY(world, wp, 0.5f, 0.5f) + 0.015f;
                    const bool northSouth = allRoadPieces(t) & dirBit(Dir::N);
                    // One shelter on each kerb, open towards the road.
                    if (northSouth) {
                        items.emplace_back(models::kBusStop, makeInstance({lx + 0.92f, y, ly + 0.5f}, facingYaw(Dir::W), 1.0f, tint));
                        items.emplace_back(models::kBusStop, makeInstance({lx + 0.08f, y, ly + 0.5f}, facingYaw(Dir::E), 1.0f, tint));
                    } else {
                        items.emplace_back(models::kBusStop, makeInstance({lx + 0.5f, y, ly + 0.92f}, facingYaw(Dir::N), 1.0f, tint));
                        items.emplace_back(models::kBusStop, makeInstance({lx + 0.5f, y, ly + 0.08f}, facingYaw(Dir::S), 1.0f, tint));
                    }
                } else if (t.type == TileType::Station && static_cast<StopKind>(t.extra) == StopKind::Rail) {
                    const Station* st = world.stations.tryGet(StationId(t.index));
                    const float y = static_cast<float>(map.corners(wp).min()) * kHeightStep;
                    const float yaw = t.rail == kTrackNS ? glm::half_pi<float>() : 0.0f;
                    items.emplace_back(models::kPlatformCanopy,
                                       makeInstance({lx + 0.5f, y, ly + 0.5f}, yaw, 1.0f,
                                                    companyTint(world, st ? st->owner : CompanyId{})));
                } else if (t.type == TileType::Station && static_cast<StopKind>(t.extra) == StopKind::Dock) {
                    const Station* st = world.stations.tryGet(StationId(t.index));
                    const Slope s = map.slope(wp);
                    const float y = static_cast<float>(flatSurfaceLevel(s, buildingSurface(s))) * kHeightStep;
                    items.emplace_back(models::kDock,
                                       makeInstance({lx + 0.5f, y, ly + 0.5f}, facingYaw(static_cast<Dir>(t.extra2 & 3)),
                                                    1.0f, companyTint(world, st ? st->owner : CompanyId{})));
                } else if (t.type == TileType::Depot) {
                    const Depot* d = world.depots.tryGet(DepotId(t.index));
                    if (!d) continue;
                    if (const TrackEdge* stub = d->edge.valid() ? world.trackEdges.tryGet(d->edge) : nullptr;
                        stub && stub->samples.size() >= 2) {
                        // Free-form track depot: at the end of its track, doors facing it.
                        const TrackSample& end = stub->samples.back();
                        const TrackSample& prev = stub->samples[stub->samples.size() - 2];
                        glm::vec2 dir(static_cast<float>(end.p.x - prev.p.x), static_cast<float>(end.p.z - prev.p.z));
                        dir = glm::length(dir) > 0.0f ? glm::normalize(dir) : glm::vec2(1, 0);
                        const glm::vec2 c = glm::vec2(end.p.x, end.p.z) / static_cast<float>(kTrackUnits) + dir * 0.4f;
                        const float yaw = std::atan2(-dir.x, -dir.y);
                        items.emplace_back(models::kRailDepot,
                                           makeInstance({c.x - o.x, trackRailY(end.h) - 0.03f, c.y - o.y}, yaw, 1.0f,
                                                        companyTint(world, d->owner)));
                        continue;
                    }
                    const Slope s = map.slope(wp);
                    const float y = static_cast<float>(flatSurfaceLevel(s, buildingSurface(s))) * kHeightStep;
                    const ModelId model = d->kind == DepotKind::Tram   ? models::kTramDepot
                                          : d->kind == DepotKind::Rail ? models::kRailDepot
                                          : d->kind == DepotKind::Ship ? models::kShipDepot
                                                                       : models::kRoadDepot;
                    items.emplace_back(model, makeInstance({lx + 0.5f, y, ly + 0.5f}, facingYaw(d->facing), 1.0f,
                                                           companyTint(world, d->owner)));
                }
            }
        }
    }
    mesh.buildings.upload(items);
    mesh.revision = active ? active->revision + 1 : 0;
    mesh.active = active != nullptr;
}

void WorldRenderer::drawChunks(const DrawLists& lists, const glm::dvec3& origin, bool shadowPass) {
    const auto offsetOf = [&](const ChunkMesh* c) {
        const TilePos o = chunkOrigin(c->pos);
        return glm::vec3(static_cast<float>(o.x - origin.x), 0.0f, static_cast<float>(o.y - origin.z));
    };
    const Shader& ts = shadowPass ? *shadowStaticShader_ : *terrainShader_;
    ts.use();
    if (!shadowPass) ts.set("uElevation", glm::vec4(elevation.strength, elevation.markLevel, kHeightStep, 0.0f));
    for (const ChunkMesh* c : lists.terrain) {
        ts.set("uOffset", offsetOf(c));
        if (!shadowPass) ts.set("uExplored", c->active ? 1.0f : 0.0f);
        c->ground.draw();
        if (!shadowPass) stats_.trianglesDrawn += static_cast<usize>(c->ground.vertexCount() / 3);
    }
    const Shader& ms = shadowPass ? *shadowModelShader_ : *modelShader_;
    ms.use();
    for (const ChunkMesh* c : lists.trees) {
        if (c->trees.total == 0) continue;
        ms.set("uOffset", offsetOf(c));
        c->trees.draw(models_);
        if (!shadowPass) stats_.treesDrawn += c->trees.total;
    }
    for (const ChunkMesh* c : lists.buildings) {
        if (c->buildings.total == 0) continue;
        ms.set("uOffset", offsetOf(c));
        c->buildings.draw(models_);
        if (!shadowPass) stats_.buildingsDrawn += c->buildings.total;
    }
    // Ground cover casts no shadows of its own.
    if (shadowPass || lists.cover.empty()) return;
    ms.set("uGroundCover", glm::vec2(kCoverFadeStart, kCoverFadeEnd));
    glDisable(GL_CULL_FACE); // its blades and petals are single faces, seen from both sides
    for (const ChunkMesh* c : lists.cover) {
        ms.set("uOffset", offsetOf(c));
        c->cover.draw(models_);
        stats_.coverDrawn += c->cover.total;
    }
    glEnable(GL_CULL_FACE);
}

void WorldRenderer::collectVehicles(const World& world, const glm::dvec3& origin, const glm::dvec3& focus,
                                    double radius, double tickFraction) {
    dynamic_.clear();
    lamps_.clear();
    glows_.clear();
    fx_.beginFrame(lastSeconds_);
    addCatenary(dynamic_, world, origin, focus, radius);
    const float night = sky_.night;
    const glm::vec3 headlight(1.0f, 0.93f, 0.8f), tailLamp(1.0f, 0.12f, 0.08f), interior(1.0f, 0.75f, 0.45f);
    const auto local = [](const glm::mat4& m, glm::vec3 p) { return glm::vec3(m * glm::vec4(p, 1.0f)); };
    const auto poseMatrix = [&](const VehiclePose& pose) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(pose.pos - origin));
        m = glm::rotate(m, pose.yaw, glm::vec3(0, 1, 0));
        return glm::rotate(m, pose.pitch, glm::vec3(0, 0, 1));
    };
    // A puff of smoke above broken down vehicles.
    const auto addSmoke = [&](const VehiclePose& pose, i32 ticksLeft) {
        const float s = 0.8f + 0.25f * std::sin(static_cast<float>(ticksLeft) * 0.15f);
        const glm::vec3 p = glm::vec3(pose.pos - origin) + glm::vec3(0.0f, 0.42f, 0.0f);
        dynamic_.add(models::kSmoke, makeInstance(p, static_cast<float>(ticksLeft) * 0.05f, s));
    };
    const auto cargoTint = [](CargoType type) {
        switch (type) {
        case CargoType::Grain: return rgba(220, 183, 87);
        case CargoType::Food: return rgba(75, 170, 98);
        case CargoType::Goods: return rgba(70, 120, 210);
        case CargoType::Wool: return rgba(238, 234, 222);
        case CargoType::Milk: return rgba(236, 240, 246);
        case CargoType::Logs: return rgba(122, 86, 56);
        case CargoType::Stone: return rgba(160, 158, 150);
        case CargoType::IronOre: return rgba(146, 84, 62);
        case CargoType::Coal: return rgba(44, 44, 48);
        case CargoType::Planks: return rgba(214, 178, 122);
        case CargoType::Cheese: return rgba(242, 196, 72);
        case CargoType::Cloth: return rgba(186, 92, 150);
        case CargoType::Bricks: return rgba(178, 76, 58);
        case CargoType::Steel: return rgba(120, 132, 148);
        case CargoType::Furniture: return rgba(150, 104, 70);
        case CargoType::Fish: return rgba(120, 170, 196);
        default: return rgba(200, 200, 200);
        }
    };
    // Loose cargo shows as heaps, everything else as crates and bundles.
    const auto cargoModel = [](CargoType type) { return cargoClass(type) == CargoClass::Bulk ? models::kGrain : models::kCrate; };
    // The cargo a car of a class is mostly carrying, for its colour.
    const auto mainCargo = [](const Vehicle& v, CargoType fallback) {
        CargoType best = fallback;
        u32 most = 0;
        for (usize c = 0; c < kCargoTypes; ++c) {
            const auto type = static_cast<CargoType>(c);
            if (cargoClass(type) != cargoClass(fallback)) continue;
            if (const u32 n = cargo::quantity(v.cargo, type); n > most) {
                most = n;
                best = type;
            }
        }
        return best;
    };
    const auto addLoad = [&](const Vehicle& v, usize index, const VehiclePose& pose, u16 model) {
        const auto& m = vehicleModel(model);
        if (!m.cargoCapacity) return;
        const auto matrix = poseMatrix(pose);
        const CargoClass k = cargoClass(m.cargoType);
        if (!m.openCargo) {
            for (const float side : {-1.0f, 1.0f}) {
                auto logo = glm::translate(matrix, glm::vec3(0, 0.17f, side * (m.type == VehicleType::Ship ? 0.235f : 0.115f)));
                dynamic_.add(k == CargoClass::Refrigerated ? models::kFoodLogo : models::kGoodsLogo, makeInstance(logo));
            }
        }
        const CargoType shown = mainCargo(v, m.cargoType);
        const u32 count = v.carLoad(index);
        const int columns = m.type == VehicleType::Ship ? 6 : 2;
        const int rows = m.type == VehicleType::Ship ? 12 : 6;
        for (u32 n = 0; n < count; ++n) {
            const float x = (static_cast<int>(n / columns) % rows - (rows - 1)*0.5f)*0.07f - 0.05f;
            const float z = (static_cast<int>(n % columns) - (columns - 1)*0.5f)*0.07f;
            float y = (m.openCargo ? 0.12f : 0.33f) + (n / (columns*rows))*0.055f;
            auto load = glm::translate(matrix, glm::vec3(x, y, z));
            dynamic_.add(cargoModel(shown), makeInstance(load, cargoTint(shown)));
        }
        if (v.state == VehicleState::Loading && v.cargoExchangeTick >= 0) {
            const double elapsed = world.clock.tick + tickFraction - v.cargoExchangeTick;
            if (elapsed >= 0 && elapsed < v.cargoExchangeUnits * 2 + 8) {
                u32 before = 0;
                for (usize i = 0; i < index && i < v.cars.size(); ++i) {
                    const auto& car = vehicleModel(v.cars[i]);
                    if (car.cargoType != CargoType::None && cargoClass(car.cargoType) == k) before += car.cargoCapacity;
                }
                for (const bool boarding : {false, true}) {
                    u32 total = 0;
                    for (usize c = 0; c < kCargoTypes; ++c)
                        if (cargoClass(static_cast<CargoType>(c)) == k) total += boarding ? v.cargoLoaded[c] : v.cargoUnloaded[c];
                    const u32 units = std::min<u32>(m.cargoCapacity, total > before ? total - before : 0);
                    const i32 first = std::max(0, static_cast<i32>((elapsed-8)/2));
                    for (u32 n = static_cast<u32>(first); n < units && n*2 <= elapsed; ++n) {
                        const float phase = static_cast<float>((elapsed - n*2)/8.0);
                        if (phase > 1) continue;
                        const float side = boarding ? 1.0f-phase : phase;
                        auto motion = glm::translate(matrix, glm::vec3(0, 0.15f, 0.1f + side * 0.5f));
                        dynamic_.add(cargoModel(shown), makeInstance(motion, cargoTint(shown)));
                    }
                }
            }
        }
    };
    // Stockpiles beside industries and at stations: a crate or heap per four units, at most
    // forty of each cargo.
    const auto addStock = [&](TilePos at, const CargoCounts& stock) {
        const double dx = at.x - focus.x, dz = at.y - focus.z;
        if (dx*dx + dz*dz > std::min(radius*radius, 10000.0)) return;
        const float ground = static_cast<float>(terrainHeightAt(world.map, at.x + 0.5, at.y + 0.5));
        u32 offset = 0;
        for (usize c = 0; c < kCargoTypes; ++c) {
            const u32 piles = std::min<u32>((stock[c] + 3) / 4, 40);
            for (u32 n = 0; n < piles; ++n) {
                const u32 slot = offset++;
                const glm::vec3 p = glm::vec3(glm::dvec3(at.x+0.1, ground, at.y+0.1) - origin) +
                    glm::vec3((slot%10)*0.075f, (slot/100)*0.07f, ((slot/10)%10)*0.075f);
                const auto type = static_cast<CargoType>(c);
                dynamic_.add(cargoModel(type), makeInstance(p, 0, 1, cargoTint(type)));
            }
        }
    };
    for (const auto& industry : world.industries)
        if (const auto* b = world.buildings.tryGet(industry.building.id); b && b->serial == industry.building.serial)
            addStock({b->pos.x, b->pos.y + b->size()}, industry.stock);
    // Smoke rising from industry chimneys.
    {
        struct Stack {
            u16 type;
            float x, z, top;
        };
        static constexpr Stack kStacks[] = {{11, 0.72f, -0.45f, 1.7f}, {20, 0.65f, -0.65f, 0.9f}, {23, 0.78f, -0.6f, 1.4f},
                                            {25, 0.65f, -0.45f, 1.5f}, {26, 0.6f, -0.9f, 1.8f},   {26, 1.0f, -0.9f, 1.8f},
                                            {27, 1.15f, -0.8f, 1.7f}};
        for (const auto& industry : world.industries) {
            const Building* b = world.buildings.tryGet(industry.building.id);
            if (!b || b->serial != industry.building.serial) continue;
            const double dx = b->pos.x - focus.x, dz = b->pos.y - focus.z;
            if (dx * dx + dz * dz > std::min(radius * radius, 40000.0)) continue;
            for (const Stack& st : kStacks) {
                if (st.type != b->type) continue;
                const float size = b->size();
                const float scale = industries::isCountryside(b->type) ? size / (b->info().size + 2) : 1.0f;
                const glm::dvec3 base(b->pos.x + size * 0.5 + st.x * scale, terrainHeightAt(world.map, b->pos.x + size * 0.5, b->pos.y + size * 0.5),
                                      b->pos.y + size * 0.5 + st.z * scale);
                fx_.addStack(base + glm::dvec3(0.0, st.top * scale, 0.0), industry.building.id.value * 8u + static_cast<u32>(&st - kStacks));
            }
        }
    }
    for (const auto [sid, station] : world.stations) {
        CargoCounts stock;
        for (usize c = 0; c < kCargoTypes; ++c) stock[c] = cargo::quantity(station.cargoWaiting, static_cast<CargoType>(c));
        addStock(station.sign, stock);
        if (station.entrances.empty()) continue;
        for (const auto& entry : station.entrances) if (!entry.tiles.empty()) {
                const double ex = entry.road.x - focus.x, ez = entry.road.y - focus.z;
                if (ex * ex + ez * ez > radius * radius) continue;
                const auto at = entry.tiles.front();
                const float y = static_cast<float>(terrainHeightAt(world.map, at.x + 0.5, at.y + 0.5));
                const float yaw =
                    static_cast<float>(std::atan2(double(entry.road.x - at.x), double(entry.road.y - at.y)));
                dynamic_.add(models::kStationEntrance,
                             makeInstance(glm::vec3(glm::dvec3(at.x + 0.5, y, at.y + 0.5) - origin), yaw, 1,
                                          companyTint(world, station.owner)));
                // Continue the paving over the reserved track margin right onto the platform.
                if (entry.path.size() >= 2 && world.trackEdges.contains(entry.platform)) {
                    const auto a = entry.path[entry.path.size() - 2], b = entry.path.back();
                    const double ax = a.x / double(kTrackUnits), az = a.z / double(kTrackUnits);
                    const double bx = b.x / double(kTrackUnits), bz = b.z / double(kTrackUnits);
                    const auto& edge = world.trackEdges[entry.platform];
                    const glm::dvec3 start(ax, terrainHeightAt(world.map, ax, az) + 0.015, az);
                    const glm::dvec3 finish(bx, world.trackNodes[edge.a].h / double(kSubLevels) * 0.5 + 0.051,
                                            bz);
                    const auto delta = finish - start;
                    const int slabs = std::max(1, static_cast<int>(std::ceil(glm::length(delta) * 2)));
                    for (int n = 0; n < slabs; ++n) {
                        const auto p = glm::mix(start, finish, (n + 0.5) / slabs);
                        auto matrix = glm::translate(glm::mat4(1), glm::vec3(p - origin));
                        matrix = glm::rotate(matrix, static_cast<float>(std::atan2(-delta.z, delta.x)),
                                             glm::vec3(0, 1, 0));
                        matrix = glm::rotate(
                            matrix, static_cast<float>(std::atan2(delta.y, std::hypot(delta.x, delta.z))),
                            glm::vec3(0, 0, 1));
                        matrix = glm::scale(
                            matrix, glm::vec3(static_cast<float>(glm::length(delta) / slabs) + 0.001f, 1, 1));
                        dynamic_.add(models::kStationApproach, makeInstance(matrix));
                    }
                }
        }
    }
    std::vector<CarPose> cars;
    for (const auto [id, v] : world.vehicles) {
        if (v.state == VehicleState::InDepot) continue;
        const double dx = v.tile.x - focus.x, dz = v.tile.y - focus.z;
        if (dx * dx + dz * dz > radius * radius) continue;
        if (v.info().type == VehicleType::Rail) {
            cars.clear();
            trainCarPoses(world, v, tickFraction, cars);
            const u32 tint = companyTint(world, v.owner);
            for (const CarPose& c : cars) {
                const auto& info = vehicleModel(c.model);
                glm::mat4 body = poseMatrix(c.pose);
                if (info.capacity) body = glm::scale(body, glm::vec3(info.length / 12.0f, 1, 1));
                dynamic_.add(vehicleModelId(info), makeInstance(body, tint));
                addLoad(v, c.index, c.pose, c.model);
            }
            fx_.addTrain(dynamic_, id, cars, origin);
            if (v.breakdownTicks > 0)
                for (const auto& car : cars) if (vehicleModel(car.model).powered) {
                    addSmoke(car.pose, v.breakdownTicks);
                    break;
                }
            if (night > 0.02f && !cars.empty()) {
                // Headlights at the front of the train lighting the line ahead, red lamps at the
                // back, and light spilling from the carriage windows.
                const glm::mat4 front = poseMatrix(cars.front().pose), back = poseMatrix(cars.back().pose);
                const float fl = vehicleModel(cars.front().model).length / 32.0f,
                            bl = vehicleModel(cars.back().model).length / 32.0f;
                const glm::vec3 ahead = glm::normalize(glm::vec3(front * glm::vec4(1, 0, 0, 0)));
                for (const float side : {-0.065f, 0.065f}) {
                    glows_.push_back({local(front, {fl + 0.015f, 0.085f, side}), headlight, 0.2f, night});
                    glows_.push_back(
                        {local(back, {-bl - 0.015f, 0.085f, side}), tailLamp, 0.12f, night * 0.9f});
                }
                lamps_.push_back({local(front, {fl + 0.06f, 0.2f, 0.0f}), 7.5f, headlight * 1.5f * night,
                                  glm::normalize(ahead + glm::vec3(0.0f, -0.16f, 0.0f)), 0.8f, 12.0f});
                for (const CarPose& c : cars)
                    if (vehicleModel(c.model).capacity > 0)
                        lamps_.push_back({local(poseMatrix(c.pose), {0.0f, 0.2f, 0.0f}),
                                          1.4f,
                                          interior * 0.45f * night,
                                          {0, -1, 0},
                                          -2.0f,
                                          -2.0f});
            }
            ++stats_.vehiclesDrawn;
            continue;
        }
        if (v.info().type == VehicleType::Tram && tramSectionCount(v) > 1) {
            std::vector<VehiclePose> sections;
            tramSectionPoses(world, v, tickFraction, sections);
            const auto tint = companyTint(world, v.owner);
            const double half = tramSectionLength(v) * 0.5;
            for (usize i = 0; i < sections.size(); ++i) {
                dynamic_.add(models::tramSection(v.info().visual, static_cast<int>(i)),
                             makeInstance(poseMatrix(sections[i]), tint));
                if (i + 1 < sections.size()) {
                    VehiclePose joint = sections[i];
                    joint.pos -=
                        glm::dvec3(std::cos(joint.yaw) * std::cos(joint.pitch), std::sin(joint.pitch),
                                   -std::sin(joint.yaw) * std::cos(joint.pitch)) *
                        half;
                    const auto& next = sections[i + 1];
                    joint.yaw = static_cast<float>(std::atan2(std::sin(joint.yaw) + std::sin(next.yaw),
                                                              std::cos(joint.yaw) + std::cos(next.yaw)));
                    joint.pitch = (joint.pitch + next.pitch) * 0.5f;
                    dynamic_.add(models::kTramBellows, makeInstance(poseMatrix(joint)));
                }
            }
            if (!sections.empty()) {
                if (v.breakdownTicks > 0) addSmoke(sections.front(), v.breakdownTicks);
                if (night > 0.02f) {
                    const auto front = poseMatrix(sections.front()), rear = poseMatrix(sections.back());
                    for (float side : {-0.06f, 0.06f}) {
                        glows_.push_back({local(front, {static_cast<float>(half) + 0.01f, 0.09f, side}),
                                          headlight, 0.14f, night});
                        glows_.push_back({local(rear, {-static_cast<float>(half) - 0.01f, 0.09f, side}),
                                          tailLamp, 0.09f, night});
                    }
                    const glm::vec3 ahead = glm::normalize(glm::vec3(front * glm::vec4(1, 0, 0, 0)));
                    lamps_.push_back({local(front, {static_cast<float>(half) + 0.04f, 0.16f, 0.0f}), 3.5f,
                                      headlight * 0.9f * night,
                                      glm::normalize(ahead + glm::vec3(0.0f, -0.25f, 0.0f)), 0.75f, 2.0f});
                }
                ++stats_.vehiclesDrawn;
            }
            continue;
        }
        const auto pose = vehiclePose(world, v, tickFraction);
        if (!pose) continue;
        const u32 tint = companyTint(world, v.owner);
        dynamic_.add(vehicleModelId(v.info()), makeInstance(poseMatrix(*pose), tint));
        addLoad(v, 0, *pose, v.model);
        if (v.breakdownTicks > 0) addSmoke(*pose, v.breakdownTicks);
        if (night > 0.02f && v.info().type != VehicleType::Ship) {
            // Buses, trams and lorries: headlights and tail lamps.
            const glm::mat4 m = poseMatrix(*pose);
            const float hl = v.info().length / 32.0f;
            for (const float side : {-0.06f, 0.06f}) {
                glows_.push_back({local(m, {hl + 0.01f, 0.08f, side}), headlight, 0.14f, night * 0.9f});
                glows_.push_back({local(m, {-hl - 0.01f, 0.08f, side}), tailLamp, 0.09f, night * 0.8f});
            }
            const glm::vec3 ahead = glm::normalize(glm::vec3(m * glm::vec4(1, 0, 0, 0)));
            lamps_.push_back({local(m, {hl + 0.04f, 0.16f, 0.0f}), 3.5f, headlight * 0.9f * night,
                              glm::normalize(ahead + glm::vec3(0.0f, -0.25f, 0.0f)), 0.75f, 2.0f});
        }
        ++stats_.vehiclesDrawn;
    }

    // Walking and seated citizens share the upload. Opaque roofs and frames hide occupants
    // naturally; glass is drawn afterwards so people are visible through the windows.
    for (const auto [id, person] : world.people) {
        if (person.state == PersonState::Driving) {
            const auto car = privateCarPose(world, person, tickFraction);
            if (!car) continue;
            const auto delta = car->pos - focus;
            if (delta.x * delta.x + delta.z * delta.z > radius * radius) continue;
            constexpr u32 colours[] = {rgba(175, 46, 39), rgba(45, 97, 160), rgba(229, 218, 178), rgba(65, 115, 75),
                                       rgba(205, 166, 48), rgba(57, 61, 68), rgba(216, 220, 224), rgba(116, 68, 127)};
            const int style = world.clock.date().year < 1970 ? 0 : static_cast<int>(person.nameSeed % 3);
            dynamic_.add(static_cast<ModelId>(models::kPrivateCarBase + style), makeInstance(poseMatrix(*car), colours[person.nameSeed % 8]));
            if (night > .02f) {
                const auto m = poseMatrix(*car);
                for (const float side : {-.055f, .055f}) {
                    glows_.push_back({local(m, {.26f, .095f, side}), headlight, .09f, night});
                    glows_.push_back({local(m, {-.24f, .095f, side}), tailLamp, .06f, night});
                }
                const auto ahead = glm::normalize(glm::vec3(m * glm::vec4(1, 0, 0, 0)));
                lamps_.push_back({local(m, {.28f, .14f, 0}), 3.0f, headlight * .65f * night,
                                  glm::normalize(ahead + glm::vec3(0, -.25f, 0)), .8f, 2.0f});
            }
            ++stats_.vehiclesDrawn;
        }
        if (person.state != PersonState::Walking && person.state != PersonState::ToStop &&
            person.state != PersonState::FromStop && person.state != PersonState::Waiting &&
            person.state != PersonState::Riding && person.state != PersonState::Driving) continue;
        if (person.state == PersonState::Riding) {
            const auto* vehicle = world.vehicles.tryGet(person.vehicle);
            if (!vehicle) continue;
            const double dx = vehicle->tile.x - focus.x, dz = vehicle->tile.y - focus.z;
            if (dx*dx + dz*dz > std::min(radius*radius, 10000.0)) continue;
        }
        const auto pose = personPose(world, person, tickFraction);
        if (!pose || !pose->visible) continue;
        const double dx = pose->pos.x - focus.x, dz = pose->pos.z - focus.z;
        if (dx * dx + dz * dz > std::min(radius * radius, 10000.0)) continue;
        const u32 tint = rgba(static_cast<u8>(70 + person.nameSeed % 150),
                             static_cast<u8>(70 + (person.nameSeed >> 8) % 150),
                             static_cast<u8>(70 + (person.nameSeed >> 16) % 150));
        const bool seated =
            person.state == PersonState::Driving || (person.state == PersonState::Riding &&
            !(person.transitMotion == 1 && world.clock.tick + tickFraction < person.transitEnd));
        dynamic_.add(seated ? models::kSeatedPerson : models::kPerson,
                     makeInstance(glm::vec3(pose->pos - origin), pose->yaw, 1.0f, tint));
        ++stats_.peopleDrawn;
    }

    // Free-form track signals: one per signalled side of a node, on the right of the track.
    for (const auto [nid, n] : world.trackNodes) {
        if (!n.signals) continue;
        const double nx = n.pos.x / double(kTrackUnits), nz = n.pos.z / double(kTrackUnits);
        const double dx = nx - focus.x, dz = nz - focus.z;
        if (dx * dx + dz * dz > radius * radius * 0.25) continue;
        for (u8 side = 0; side < 2; ++side) {
            if (!(n.signals & (1u << side))) continue;
            bool clear = false;
            for (u8 slot = 0; slot < 2; ++slot) {
                const TrackEdgeId e = n.edges[side * 2u + slot];
                if (e.valid() && world.trackEdges[e].reservedBy.valid()) clear = true;
            }
            const TDir d = n.side(side);
            const glm::vec2 travel(d.x / float(kDirOne), d.z / float(kDirOne));
            const glm::vec2 right(-travel.y, travel.x);
            const glm::vec2 at = glm::vec2(float(nx), float(nz)) + right * 0.42f - travel * 0.15f;
            const float yaw = std::atan2(travel.y, -travel.x);
            dynamic_.add(models::kSignal,
                         makeInstance(glm::vec3(at.x - float(origin.x), trackRailY(n.h) - 0.03f, at.y - float(origin.z)), yaw,
                                      1.0f, clear ? rgba(60, 230, 90) : rgba(235, 50, 36)));
        }
    }
    // Signals: red unless a train has reserved a path through them.
    constexpr u32 kRed = rgba(235, 50, 36), kGreen = rgba(60, 230, 90);
    for (const u64 key : world.signalTiles) {
        const ChunkPos k = ChunkPos::fromKey(key);
        const TilePos tp{k.x, k.y};
        const double dx = tp.x + 0.5 - focus.x, dz = tp.y + 0.5 - focus.z;
        if (dx * dx + dz * dz > radius * radius * 0.25) continue;
        const Tile* t = world.map.tile(tp);
        if (!t) continue;
        if (t->type == TileType::Road && t->rail) {
            const bool nsRoad = t->rail == kTrackEW;
            const u32 tint = (t->extra2 & t->rail) ? kRed : kGreen;
            for (const float side : {-1.0f, 1.0f}) {
                const glm::vec2 at = nsRoad ? glm::vec2(0.86f, 0.5f + side * 0.4f)
                                          : glm::vec2(0.5f + side * 0.4f, 0.86f);
                const float y = roadSurfaceY(world, tp, at.x, at.y);
                const Dir face = nsRoad ? (side < 0 ? Dir::N : Dir::S) : (side < 0 ? Dir::W : Dir::E);
                dynamic_.add(models::kSignal,
                             makeInstance(glm::vec3(tp.x + at.x - origin.x, y, tp.y + at.y - origin.z),
                                          facingYaw(face), 1.0f, tint));
            }
            continue;
        }
        if (t->type != TileType::Rail || !t->extra) continue;
        const bool ns = t->rail == kTrackNS;
        for (const u8 bit : {kSignalForward, kSignalBackward}) {
            if (!(t->extra & bit)) continue;
            // Direction of travel the signal applies to; it stands on the right of the track,
            // facing the approaching train.
            const glm::vec2 travel = ns ? glm::vec2(0.0f, bit == kSignalForward ? -1.0f : 1.0f)
                                        : glm::vec2(bit == kSignalForward ? 1.0f : -1.0f, 0.0f);
            const Dir direction = ns ? (bit == kSignalForward ? Dir::N : Dir::S)
                                     : (bit == kSignalForward ? Dir::E : Dir::W);
            const u32 tint = (t->extra & kSignalBlock)
                                 ? (blockSignalClear(world, tp, direction) ? kGreen : kRed)
                                 : ((t->extra2 & t->rail) ? kGreen : kRed);
            const glm::vec2 right(-travel.y, travel.x);
            const glm::vec2 at = glm::vec2(0.5f) + right * 0.24f;
            const float y = roadSurfaceY(world, tp, at.x, at.y);
            const float yaw = std::atan2(travel.y, -travel.x); // lamp (+X) faces against travel
            dynamic_.add(models::kSignal,
                         makeInstance(glm::vec3(tp.x + at.x - origin.x, y, tp.y + at.y - origin.z), yaw,
                                      (t->extra & kSignalBlock) ? 1.2f : 1.0f, tint));
        }
    }
    dynamic_.upload();
}

void WorldRenderer::render(const World& world, const Camera& cam, double minuteOfDay, double timeSeconds,
                           double tickFraction, const std::vector<OverlayQuad>& overlays) {
    const Map& map = world.map;
    ++frame_;
    stats_ = {};
    const glm::dvec3 origin(std::floor(cam.target.x), 0.0, std::floor(cam.target.z));
    const glm::mat4 view = cam.viewMatrix(origin);
    const glm::mat4 proj = cam.projectionMatrix();
    const glm::mat4 viewProj = proj * view;
    sky_ = computeSky(minuteOfDay);
    // Weather: the world's, or a fixed one for automation. Overcast skies dim the sun, lift the
    // shade and wash the colour out of the sky.
    weather_ = computeWeather(world.settings.seed, static_cast<double>(world.clock.tick) / kTicksPerDay, minuteOfDay);
    if (settings.forceCloudCover >= 0.0f) weather_.cover = settings.forceCloudCover;
    if (settings.forceMist >= 0.0f) weather_.mist = settings.forceMist;
    if (!settings.clouds) weather_.cover = 0.0f;
    weather_.mist *= settings.fogStrength;
    if (!settings.weather) weather_ = {};
    {
        const float dt = lastSeconds_ < 0.0 ? 0.0f : static_cast<float>(std::clamp(timeSeconds - lastSeconds_, 0.0, 0.25));
        lastSeconds_ = timeSeconds;
        cloudDrift_ += weather_.wind * (1.6f + 2.4f * weather_.cover) * dt;
        // Mist fills the low ground around the view: below the land the camera looks at.
        const float ground = static_cast<float>(cam.target.y) + 0.6f;
        mistLevel_ = lastSeconds_ == timeSeconds && dt == 0.0f ? ground : mistLevel_ + (ground - mistLevel_) * std::min(1.0f, dt * 0.8f);
        const float cover2 = weather_.cover * weather_.cover;
        sky_.sunStrength *= 1.0f - 0.62f * cover2;
        const auto grey = [&](glm::vec3 c, float k) { return glm::mix(c, glm::vec3(glm::dot(c, glm::vec3(0.3f, 0.59f, 0.11f))), k); };
        sky_.zenith = grey(sky_.zenith, weather_.cover * 0.6f);
        sky_.horizon = grey(sky_.horizon, weather_.cover * 0.45f);
        sky_.ambientSky = glm::mix(sky_.ambientSky, sky_.ambientSky * 1.22f, cover2);
    }
    track_.sync(world);

    // --- Determine visible chunks, building meshes nearest first -------------------------
    const double viewRadius = std::min<double>(settings.maxViewDistance, cam.distance * 2.6 + 120.0);
    const double shadowRadius = std::min(viewRadius, cam.distance * 1.1 + 24.0);
    const auto lodFor = [&](double d) { return d < 150.0 * settings.detailScale ? 0 : (d < 300.0 * settings.detailScale ? 1 : 2); };
    const double treeRadius = std::min<double>(viewRadius, settings.treeDistance);
    // How densely a chunk this far away (at this level of detail) grows ground cover, with some
    // slack before a chunk that has it loses it.
    const auto coverFor = [&](double dist, int lod, const ChunkMesh* built) {
        const double reach = coverRadius(settings.groundDetail) + (built && built->coverDensity > 0.0f ? 16.0 : 0.0);
        return lod == 0 && dist < reach ? coverDensity(settings.groundDetail) : 0.0f;
    };
    const double buildingRadius = std::min(viewRadius, 420.0);
    const Frustum frustum = extractFrustum(viewProj);
    struct Candidate {
        ChunkPos pos;
        double dist2;
    };
    std::vector<Candidate> candidates;
    const i32 cx0 = static_cast<i32>(std::floor((cam.target.x - viewRadius) / kChunkSize));
    const i32 cx1 = static_cast<i32>(std::floor((cam.target.x + viewRadius) / kChunkSize));
    const i32 cz0 = static_cast<i32>(std::floor((cam.target.z - viewRadius) / kChunkSize));
    const i32 cz1 = static_cast<i32>(std::floor((cam.target.z + viewRadius) / kChunkSize));
    const double maxDist = viewRadius + kChunkSize * 0.25;
    for (i32 cz = cz0; cz <= cz1; ++cz) {
        for (i32 cx = cx0; cx <= cx1; ++cx) {
            const double dx = (cx + 0.5) * kChunkSize - cam.target.x;
            const double dz = (cz + 0.5) * kChunkSize - cam.target.z;
            const double d2 = dx * dx + dz * dz;
            if (d2 > maxDist * maxDist) continue;
            candidates.push_back({{cx, cz}, d2});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.dist2 < b.dist2; });

    // Workers own snapshots, including border water and building classifications.
    // Only the render thread accesses the live world or uploads GL resources.
    std::unordered_map<u64, std::future<ChunkGeometry>> jobs;
    const auto meshStart = std::chrono::steady_clock::now();
    for (const auto& cand : candidates) {
        if (jobs.size() >= static_cast<usize>(std::clamp(settings.meshBuildsPerFrame, 1, 4))) break;
        const auto cached = meshes_.find(cand.pos.key());
        const auto* active = map.chunk(cand.pos);
        const u32 wanted = active ? active->revision + 1 : 0;
        const double dist = std::sqrt(cand.dist2);
        int lod = lodFor(dist);
        if (cached != meshes_.end() && cached->second.lod != lod &&
            (lodFor(dist + 16) == cached->second.lod || lodFor(dist - 16) == cached->second.lod)) lod = cached->second.lod;
        const float cover = coverFor(dist, lod, cached != meshes_.end() ? &cached->second : nullptr);
        if (cached != meshes_.end() && cached->second.revision == wanted && cached->second.lod == lod &&
            cached->second.coverDensity == cover)
            continue;
        const auto o = chunkOrigin(cand.pos);
        const float minY = cached != meshes_.end() ? cached->second.minY : -4.0f;
        const float maxY = cached != meshes_.end() ? cached->second.maxY : 30.0f;
        const glm::vec3 mn(static_cast<float>(o.x-origin.x), minY, static_cast<float>(o.y-origin.z));
        if (!frustum.intersects(mn, {mn.x+kChunkSize, maxY, mn.z+kChunkSize}) &&
            !(settings.shadows && cand.dist2 < (shadowRadius+24)*(shadowRadius+24))) continue;
        std::optional<Chunk> snapshot;
        std::set<u32> residential;
        std::map<TilePos, u8> curved; // tiles of curved roads (1) and their verges (2), see buildChunkGeometry
        if (active) {
            snapshot = *active;
            for (const auto& tile : active->tiles) if (tile.type == TileType::Building) {
                const auto* b = world.buildings.tryGet(BuildingId(tile.index));
                if (!b || b->info().category == BuildingCategory::Residential || b->type == 12 || (b->type >= 15 && b->type <= 18)) residential.insert(tile.index);
            }
            if (!world.roadCurves.empty())
                for (i32 ly = 0; ly < kChunkSize; ++ly)
                    for (i32 lx = 0; lx < kChunkSize; ++lx)
                        if (const u8 kind = curvedRoadAt(world, {o.x + lx, o.y + ly})) curved[{o.x + lx, o.y + ly}] = kind;
        }
        std::map<TilePos, bool> border;
        std::map<TilePos, Tile> borderTiles; // explored ones
        for (i32 n = -1; n <= kChunkSize; ++n) {
            for (const auto t : {TilePos{o.x+n,o.y-1}, TilePos{o.x+n,o.y+kChunkSize},
                                 TilePos{o.x-1,o.y+n}, TilePos{o.x+kChunkSize,o.y+n}}) {
                border[t] = tileIsWater(map,t);
                if (const Tile* tile = map.tile(t)) borderTiles[t] = *tile;
            }
        }
        jobs.emplace(cand.pos.key(), std::async(std::launch::async,
            [snapshot = std::move(snapshot), residential = std::move(residential), curved = std::move(curved),
             border = std::move(border), borderTiles = std::move(borderTiles), gen = map.generator(), pos = cand.pos, lod,
             cover]() mutable {
                if (!snapshot) { snapshot.emplace(); gen.generateChunk(pos, *snapshot); }
                return buildChunkGeometry(*snapshot, gen, [&](TilePos t) { return border.at(t); }, lod, nullptr,
                    [&](u32 id) { return residential.contains(id); }, [&](TilePos t) -> const Tile* {
                        const auto it = borderTiles.find(t);
                        return it == borderTiles.end() ? nullptr : &it->second;
                    }, cover, [&](TilePos t) -> u8 {
                        const auto it = curved.find(t);
                        return it == curved.end() ? 0 : it->second;
                    });
            }));
    }
    DrawLists main, shadow;
    int builds = 0;
    for (const Candidate& cand : candidates) {
        const TilePos o = chunkOrigin(cand.pos);
        auto it = meshes_.find(cand.pos.key());
        const float minY = it != meshes_.end() ? it->second.minY : -4.0f;
        const float maxY = it != meshes_.end() ? it->second.maxY : 30.0f;
        const glm::vec3 mn(static_cast<float>(o.x - origin.x), minY, static_cast<float>(o.y - origin.z));
        const glm::vec3 mx(mn.x + kChunkSize, maxY, mn.z + kChunkSize);
        const bool inFrustum = frustum.intersects(mn, mx);
        const bool caster = settings.shadows && cand.dist2 < (shadowRadius + 24) * (shadowRadius + 24);
        if (!inFrustum && !caster) continue;
        const Chunk* active = map.chunk(cand.pos);
        const u32 wanted = active ? active->revision + 1 : 0;
        // Level of detail by distance, with hysteresis to avoid rebuild churn at the boundaries.
        const double dist = std::sqrt(cand.dist2);
        int lod = lodFor(dist);
        if (it != meshes_.end() && it->second.lod != lod &&
            (lodFor(dist + 16.0) == it->second.lod || lodFor(dist - 16.0) == it->second.lod))
            lod = it->second.lod;
        const float cover = coverFor(dist, lod, it != meshes_.end() ? &it->second : nullptr);
        if ((it == meshes_.end() || it->second.revision != wanted || it->second.lod != lod || it->second.coverDensity != cover) &&
            jobs.contains(cand.pos.key())) {
            ChunkMesh& mesh = meshes_[cand.pos.key()];
            const auto waitStart = std::chrono::steady_clock::now();
            const auto geometry = jobs.at(cand.pos.key()).get();
            stats_.meshWaitMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-waitStart).count();
            buildChunkMesh(world, cand.pos, lod, mesh, &geometry, cover);
            ++builds;
            it = meshes_.find(cand.pos.key());
        }
        if (it == meshes_.end()) continue;
        ChunkMesh& mesh = it->second;
        mesh.lastUsedFrame = frame_;
        const bool trees = cand.dist2 < treeRadius * treeRadius;
        const bool buildings = cand.dist2 < buildingRadius * buildingRadius;
        if (inFrustum) {
            main.terrain.push_back(&mesh);
            if (trees) main.trees.push_back(&mesh);
            if (buildings) main.buildings.push_back(&mesh);
            // Ground cover only where some of it is still standing: close enough to the eye.
            if (mesh.cover.total > 0) {
                const double ex = std::clamp(cam.eye().x, static_cast<double>(o.x), static_cast<double>(o.x + kChunkSize)) - cam.eye().x;
                const double ez = std::clamp(cam.eye().z, static_cast<double>(o.y), static_cast<double>(o.y + kChunkSize)) - cam.eye().z;
                const double ey = std::max(0.0, cam.eye().y - mesh.maxY);
                if (ex * ex + ez * ez + ey * ey < kCoverFadeEnd * kCoverFadeEnd) main.cover.push_back(&mesh);
            }
        }
        if (caster) {
            shadow.terrain.push_back(&mesh);
            if (trees) shadow.trees.push_back(&mesh);
            shadow.buildings.push_back(&mesh);
        }
    }
    stats_.meshCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-meshStart).count();
    stats_.meshesBuilt = builds;
    stats_.chunksVisible = static_cast<int>(main.terrain.size());
    if (frame_ % 60 == 0) {
        std::erase_if(meshes_, [&](const auto& kv) { return kv.second.lastUsedFrame + 180 < frame_; });
    }
    stats_.chunksCached = static_cast<int>(meshes_.size());

    collectVehicles(world, origin, cam.target, buildingRadius, tickFraction);
    collectLamps(world, origin, cam.target, viewRadius);

    // --- Per frame uniforms ---------------------------------------------------------------
    const glm::vec3 focus(static_cast<float>(cam.target.x - origin.x), static_cast<float>(cam.target.y),
                          static_cast<float>(cam.target.z - origin.z));
    if (settings.shadows) ensureShadowTarget();
    const bool shadows = settings.shadows && sky_.sunStrength > 0.01f;
    glm::mat4 lightViewProj(1.0f);
    const float shadowTexel = 1.0f / static_cast<float>(shadowSize_);
    if (shadows) {
        const float radius = std::ceil(static_cast<float>(shadowRadius) / 8.0f) * 8.0f;
        const glm::vec3 up = std::abs(sky_.sunDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 lightView = glm::lookAt(focus + sky_.sunDir * 300.0f, focus, up);
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 1.0f, 700.0f);
        // Snap to shadow texels to avoid shimmering while the camera moves.
        glm::vec4 so = lightProj * lightView * glm::vec4(0, 0, 0, 1);
        const float half = static_cast<float>(shadowSize_) * 0.5f;
        so *= half;
        lightProj[3][0] += (std::round(so.x) - so.x) / half;
        lightProj[3][1] += (std::round(so.y) - so.y) / half;
        lightViewProj = lightProj * lightView;
    }

    FrameUniforms fu{};
    fu.viewProj = viewProj;
    fu.lightViewProj = lightViewProj;
    const glm::dvec3 eye = cam.eye() - origin;
    fu.cameraPos = glm::vec4(glm::vec3(eye), static_cast<float>(timeSeconds));
    fu.sunDir = glm::vec4(sky_.sunDir, sky_.sunStrength);
    fu.sunColor = glm::vec4(sky_.sunColor, sky_.night);
    fu.zenithColor = glm::vec4(sky_.zenith, 1.0f);
    fu.horizonColor = glm::vec4(sky_.horizon, settings.nightFogStrength);
    fu.ambientSky = glm::vec4(sky_.ambientSky, 1.0f);
    fu.ambientGround = glm::vec4(sky_.ambientGround, 1.0f);
    const float fogEnd = static_cast<float>(viewRadius) - kChunkSize * 0.75f;
    fu.fogParams = glm::vec4(fogEnd * 0.5f, fogEnd, focus.x, focus.z);
    fu.shadowParams = glm::vec4(shadowTexel, 0.0006f, shadows ? 1.0f : 0.0f, 0.04f);
    fu.weather = glm::vec4(weather_.cover, weather_.mist, cloudDrift_.x, cloudDrift_.y);
    fu.origin = glm::vec4(static_cast<float>(origin.x), kCloudHeight, static_cast<float>(origin.z), mistLevel_);
    fu.visualOptions = glm::vec4(settings.brightness, settings.cloudShadows ? 1.0f : 0.0f,
                                 settings.treeSway ? 1.0f : 0.0f, settings.waterAnimation ? 1.0f : 0.0f);
    {
        // The lamps nearest the view (headlights count as much nearer) light the scene.
        const auto score = [&](const Lamp& l) {
            return glm::length(glm::vec2(l.pos.x - focus.x, l.pos.z - focus.z)) - l.priority;
        };
        const usize n = std::min<usize>(lamps_.size(), kMaxLamps);
        std::partial_sort(lamps_.begin(), lamps_.begin() + static_cast<std::ptrdiff_t>(n), lamps_.end(),
                          [&](const Lamp& a, const Lamp& b) { return score(a) < score(b); });
        for (usize i = 0; i < n; ++i) {
            fu.lightPos[i] = glm::vec4(lamps_[i].pos, lamps_[i].reach);
            fu.lightColor[i] = glm::vec4(lamps_[i].colour, 0.0f);
            fu.lightDir[i] = glm::vec4(lamps_[i].dir, lamps_[i].cosCone);
        }
        fu.lightCount = glm::vec4(static_cast<float>(n), 0.0f, 0.0f, 0.0f);
    }
    glBindBuffer(GL_UNIFORM_BUFFER, frameUbo_.get());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameUniforms), &fu);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // --- Shadow pass ------------------------------------------------------------------------
    if (shadows) {
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_.get());
        glViewport(0, 0, shadowSize_, shadowSize_);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 3.0f);
        drawChunks(shadow, origin, true);
        shadowStaticShader_->use();
        track_.draw(*shadowStaticShader_, origin, cam.target, shadowRadius, true);
        shadowModelShader_->use();
        shadowModelShader_->set("uOffset", glm::vec3(0.0f));
        dynamic_.draw(models_);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // --- Main pass --------------------------------------------------------------------------
    glViewport(0, 0, cam.viewportWidth(), cam.viewportHeight());
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    skyShader_->use();
    skyShader_->set("uInvViewProj", glm::inverse(viewProj));
    glBindVertexArray(emptyVao_.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_.get());
    glActiveTexture(GL_TEXTURE0);

    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    drawChunks(main, origin, false);
    terrainShader_->use();
    terrainShader_->set("uElevation", glm::vec4(0.0f, -1000.0f, kHeightStep, 0.0f)); // track itself is not tinted
    stats_.trianglesDrawn += track_.draw(*terrainShader_, origin, cam.target, viewRadius, false);
    modelShader_->use();
    modelShader_->set("uOffset", glm::vec3(0.0f));
    dynamic_.draw(models_);
    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Glazing follows all opaque bodies and passengers, with no depth writes. This preserves
    // roofs and prevents a pane from hiding a person drawn later in the instance batch.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    dynamic_.draw(models_, true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Water (translucent)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    waterShader_->use();
    for (const ChunkMesh* c : main.terrain) {
        if (c->water.empty()) continue;
        const TilePos o = chunkOrigin(c->pos);
        waterShader_->set("uOffset",
                          glm::vec3(static_cast<float>(o.x - origin.x), 0.0f, static_cast<float>(o.y - origin.z)));
        waterShader_->set("uWavePhase", glm::vec2(static_cast<float>(o.x & 1023), static_cast<float>(o.y & 1023)));
        c->water.draw();
    }
    glDepthMask(GL_TRUE);

    // Halos around lamps at night.
    if (settings.lightHalos) drawGlows(view);
    // Steam and smoke.
    if (settings.particles) fx_.draw(view, origin);

    // Clouds: low poly puffs where the cloud field is thick, drifting with the wind. They show
    // once the camera is well above the land, fading in as it rises, and are skipped above it.
    if (weather_.cover > 0.02f && cam.distance > 80.0) {
        clouds_.clear();
        const float fade = static_cast<float>(glm::smoothstep(80.0, 170.0, cam.distance));
        constexpr float kCell = 26.0f;
        const glm::vec2 centre(static_cast<float>(cam.target.x), static_cast<float>(cam.target.z));
        const float reach = static_cast<float>(viewRadius);
        // Cells in drifting cloud space, so each puff keeps its place in the field as it moves.
        const glm::vec2 lo = glm::floor((centre - reach + cloudDrift_) / kCell), hi = glm::floor((centre + reach + cloudDrift_) / kCell);
        for (float cy = lo.y; cy <= hi.y; cy += 1.0f)
            for (float cx = lo.x; cx <= hi.x; cx += 1.0f) {
                const float h = hash12(glm::vec2(cx, cy) * 1.7f + 0.3f);
                const glm::vec2 inCloud = (glm::vec2(cx, cy) + glm::vec2(0.25f + h * 0.5f, 0.75f - h * 0.5f)) * kCell;
                const glm::vec2 at = inCloud - cloudDrift_;
                if (glm::length(at - centre) > reach) continue;
                const float density = cloudField(at, cloudDrift_, weather_.cover);
                if (density < 0.3f) continue;
                const float y = kCloudHeight + h * 6.0f;
                if (y > static_cast<float>(cam.eye().y) - 6.0f) continue;
                const float scale = 5.0f + density * 7.0f + h * 3.0f;
                ModelInstance inst = makeInstance(glm::vec3(at.x - static_cast<float>(origin.x), y, at.y - static_cast<float>(origin.z)),
                                                  h * 6.2831853f, scale);
                inst.tint = rgba(255, 255, 255, static_cast<u8>(std::clamp(fade * (0.45f + 0.45f * density), 0.0f, 1.0f) * 255.0f));
                clouds_.add(static_cast<ModelId>(models::kCloud0 + static_cast<int>(h * 3.0f) % 3), inst);
            }
        clouds_.upload();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        cloudShader_->use();
        clouds_.draw(models_);
        glDepthMask(GL_TRUE);
    }

    // Overlays
    if (!overlays.empty()) {
        overlayShader_->use();
        overlayShader_->set("uOffset", glm::vec3(0.0f));
        glDisable(GL_CULL_FACE);
        const auto build = [&](bool hidden) {
            MeshBuilder mb;
            for (const OverlayQuad& q : overlays) {
                if (hidden && !q.xray) continue;
                glm::vec3 p[4];
                for (int i = 0; i < 4; ++i) p[i] = glm::vec3(q.corners[i] - origin);
                // Hidden parts at a third of the opacity: a ghost through the hillside.
                const u32 c = hidden ? (q.color & 0x00ffffffu) | (((q.color >> 24) / 3) << 24) : q.color;
                mb.quad(p[0], p[1], p[2], p[3], c);
            }
            return mb;
        };
        if (const MeshBuilder hidden = build(true); !hidden.vertices.empty()) {
            overlayMesh_.upload(hidden.vertices);
            glDepthFunc(GL_GREATER);
            glDepthMask(GL_FALSE);
            overlayMesh_.draw();
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }
        overlayMesh_.upload(build(false).vertices);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
        overlayMesh_.draw();
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
}

} // namespace tp::gfx
