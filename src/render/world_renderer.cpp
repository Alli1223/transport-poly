#include "render/world_renderer.hpp"

#include "core/hash.hpp"
#include "core/log.hpp"
#include "render/shaders.hpp"
#include "render/picking.hpp"
#include <future>
#include <chrono>
#include <set>
#include "render/vehicle_pose.hpp"
#include "render/person_pose.hpp"
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
    if (const Tile* tile = map.tile(t)) return tile->type == TileType::Water || waterUnderBridge(*tile);
    const TerrainGenerator& g = map.generator();
    return g.rawHeight(t.x, t.y) < 0 && g.rawHeight(t.x + 1, t.y) < 0 && g.rawHeight(t.x + 1, t.y + 1) < 0 &&
           g.rawHeight(t.x, t.y + 1) < 0;
}

ModelId treeModel(const TreePlacement& t) {
    if (t.snowy) return models::kTreeConiferSnow;
    const ModelId base = t.type == TreeType::Conifer ? models::kTreeConifer0 : models::kTreeDeciduous0;
    return static_cast<ModelId>(base + t.variant % 3);
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
    shadowStaticShader_ = Shader::create("shadow-static", shaders::shadowStaticVS(), shaders::shadowFS());
    shadowModelShader_ = Shader::create("shadow-model", shaders::shadowModelVS(), shaders::shadowFS());
    overlayShader_ = Shader::create("overlay", shaders::overlayVS(), shaders::overlayFS());
    if (!terrainShader_ || !modelShader_ || !waterShader_ || !skyShader_ || !shadowStaticShader_ ||
        !shadowModelShader_ || !overlayShader_)
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
    ensureShadowTarget();
    return true;
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

void WorldRenderer::buildChunkMesh(const World& world, ChunkPos pos, int lod, ChunkMesh& mesh, const ChunkGeometry* prepared) {
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
        *source, map.generator(), [&](TilePos t) { return tileIsWater(map, t); }, lod, &world);
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
    if (active) {
        const TilePos o = chunkOrigin(pos);
        for (i32 ly = 0; ly < kChunkSize; ++ly) {
            for (i32 lx = 0; lx < kChunkSize; ++lx) {
                const Tile& t = active->tile(lx, ly);
                if (t.type != TileType::Building || t.extra != 0) continue;
                const Building* b = world.buildings.tryGet(BuildingId(t.index));
                if (!b) continue;
                const u8 size = b->info().size;
                // Surface level of the footprint (flat or on a foundation).
                const Slope s = map.slope(b->pos);
                const float y = static_cast<float>(flatSurfaceLevel(s, buildingSurface(s))) * kHeightStep;
                const glm::vec3 p(static_cast<float>(b->pos.x - o.x) + size * 0.5f, y,
                                  static_cast<float>(b->pos.y - o.y) + size * 0.5f);
                items.emplace_back(models::building(b->type, b->variant),
                                   makeInstance(p, facingYaw(b->facing), 1.0f, buildingTint(*b)));
                mesh.maxY = std::max(mesh.maxY, y + 4.6f);
            }
        }
        // Stop shelters and depots.
        for (i32 ly = 0; ly < kChunkSize; ++ly) {
            for (i32 lx = 0; lx < kChunkSize; ++lx) {
                const Tile& t = active->tile(lx, ly);
                const TilePos wp{o.x + lx, o.y + ly};
                if (t.type == TileType::Road && t.roadOwner == kOwnerTown && !t.span && !t.rail &&
                    ((static_cast<u32>(wp.x) + static_cast<u32>(wp.y)) & 3u) == 0) {
                    const float y = roadSurfaceY(world, wp, 0.08f, 0.08f) + 0.02f;
                    items.emplace_back(models::kStreetLamp, makeInstance({lx + 0.08f, y, ly + 0.08f}, 0.0f, 1.0f));
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
}

void WorldRenderer::collectVehicles(const World& world, const glm::dvec3& origin, const glm::dvec3& focus,
                                    double radius, double tickFraction) {
    dynamic_.clear();
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
        return type == CargoType::Grain ? rgba(220,183,87) : type == CargoType::Food ? rgba(75,170,98) : rgba(70,120,210);
    };
    const auto addLoad = [&](const Vehicle& v, usize index, const VehiclePose& pose, u16 model) {
        const auto& m = vehicleModel(model);
        if (!m.cargoCapacity) return;
        const auto matrix = poseMatrix(pose);
        if (!m.openCargo) {
            for (const float side : {-1.0f, 1.0f}) {
                auto logo = glm::translate(matrix, glm::vec3(0, 0.17f, side * (m.type == VehicleType::Ship ? 0.235f : 0.115f)));
                dynamic_.add(m.cargoType == CargoType::Food ? models::kFoodLogo : models::kGoodsLogo, makeInstance(logo));
            }
        }
        const u32 count = v.carLoad(index);
        const int columns = m.type == VehicleType::Ship ? 6 : 2;
        const int rows = m.type == VehicleType::Ship ? 12 : 6;
        for (u32 n = 0; n < count; ++n) {
            const float x = (static_cast<int>(n / columns) % rows - (rows - 1)*0.5f)*0.07f - 0.05f;
            const float z = (static_cast<int>(n % columns) - (columns - 1)*0.5f)*0.07f;
            float y = (m.openCargo ? 0.12f : 0.33f) + (n / (columns*rows))*0.055f;
            auto load = glm::translate(matrix, glm::vec3(x, y, z));
            dynamic_.add(m.cargoType == CargoType::Grain ? models::kGrain : models::kCrate,
                         makeInstance(load, cargoTint(m.cargoType)));
        }
        if (v.state == VehicleState::Loading && v.cargoExchangeTick >= 0) {
            const double elapsed = world.clock.tick + tickFraction - v.cargoExchangeTick;
            if (elapsed >= 0 && elapsed < v.cargoExchangeUnits * 2 + 8) {
                u32 before = 0;
                for (usize k = 0; k < index && k < v.cars.size(); ++k) {
                    const auto& car = vehicleModel(v.cars[k]);
                    if (car.cargoType == m.cargoType) before += car.cargoCapacity;
                }
                const usize commodity = static_cast<usize>(m.cargoType);
                for (const bool boarding : {false, true}) {
                    const u32 total = boarding ? v.cargoLoaded[commodity] : v.cargoUnloaded[commodity];
                    const u32 units = std::min<u32>(m.cargoCapacity, total > before ? total - before : 0);
                    const i32 first = std::max(0, static_cast<i32>((elapsed-8)/2));
                    for (u32 n = static_cast<u32>(first); n < units && n*2 <= elapsed; ++n) {
                        const float phase = static_cast<float>((elapsed - n*2)/8.0);
                        if (phase > 1) continue;
                        const float side = boarding ? 1.0f-phase : phase;
                        auto motion = glm::translate(matrix, glm::vec3(0, 0.15f, 0.1f + side * 0.5f));
                        dynamic_.add(m.cargoType == CargoType::Grain ? models::kGrain : models::kCrate,
                                     makeInstance(motion, cargoTint(m.cargoType)));
                    }
                }
            }
        }
    };
    const auto addStock = [&](TilePos at, const std::array<u32, kCargoTypes>& stock) {
        const double dx = at.x - focus.x, dz = at.y - focus.z;
        if (dx*dx + dz*dz > std::min(radius*radius, 10000.0)) return;
        const float ground = static_cast<float>(terrainHeightAt(world.map, at.x + 0.5, at.y + 0.5));
        u32 offset = 0;
        for (usize c = 0; c < kCargoTypes; ++c) {
            for (u32 n = 0; n < stock[c]; ++n) {
                const u32 slot = offset++;
                const glm::vec3 p = glm::vec3(glm::dvec3(at.x+0.1, ground, at.y+0.1) - origin) +
                    glm::vec3((slot%10)*0.075f, (slot/100)*0.07f, ((slot/10)%10)*0.075f);
                dynamic_.add(c == 0 ? models::kGrain : models::kCrate, makeInstance(p, 0, 1, cargoTint(static_cast<CargoType>(c))));
            }
        }
    };
    for (const auto& industry : world.industries)
        if (const auto* b = world.buildings.tryGet(industry.building.id); b && b->serial == industry.building.serial)
            addStock({b->pos.x, b->pos.y + b->info().size}, industry.stock);
    for (const auto [sid, station] : world.stations) {
        std::array<u32, kCargoTypes> stock{};
        for (usize c = 0; c < kCargoTypes; ++c) stock[c] = cargo::quantity(station.cargoWaiting, static_cast<CargoType>(c));
        addStock(station.sign, stock);
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
                const u32 carTint = info.capacity && v.carLoad(c.index) && dx*dx + dz*dz < 10000 ? (tint & 0x00ffffffu) | (253u << 24) : tint;
                dynamic_.add(vehicleModelId(info), makeInstance(poseMatrix(c.pose), carTint));
                addLoad(v, c.index, c.pose, c.model);
            }
            if (v.breakdownTicks > 0 && !cars.empty()) addSmoke(cars.front().pose, v.breakdownTicks);
            ++stats_.vehiclesDrawn;
            continue;
        }
        const auto pose = vehiclePose(world, v, tickFraction);
        if (!pose) continue;
        u32 tint = companyTint(world, v.owner);
        if (!v.passengers.empty() && dx*dx + dz*dz < 10000) tint = (tint & 0x00ffffffu) | (253u << 24);
        dynamic_.add(vehicleModelId(v.info()), makeInstance(poseMatrix(*pose), tint));
        addLoad(v, 0, *pose, v.model);
        if (v.breakdownTicks > 0) addSmoke(*pose, v.breakdownTicks);
        ++stats_.vehiclesDrawn;
    }

    // Pedestrians share the instanced model upload with vehicles. Indoor citizens
    // and passengers already represented by a vehicle need no separate geometry.
    for (const auto [id, person] : world.people) {
        if (person.state != PersonState::Walking && person.state != PersonState::ToStop &&
            person.state != PersonState::FromStop && person.state != PersonState::Waiting &&
            person.state != PersonState::Riding) continue;
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
        dynamic_.add(models::kPerson, makeInstance(glm::vec3(pose->pos - origin), pose->yaw, 1.0f, tint));
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
    track_.sync(world);

    // --- Determine visible chunks, building meshes nearest first -------------------------
    const double viewRadius = std::min<double>(settings.maxViewDistance, cam.distance * 2.6 + 120.0);
    const double shadowRadius = std::min(viewRadius, cam.distance * 1.1 + 24.0);
    const double treeRadius = std::min(viewRadius, 200.0);
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
        const auto lodFor = [](double d) { return d < 150 ? 0 : d < 300 ? 1 : 2; };
        const double dist = std::sqrt(cand.dist2);
        int lod = lodFor(dist);
        if (cached != meshes_.end() && cached->second.lod != lod &&
            (lodFor(dist + 16) == cached->second.lod || lodFor(dist - 16) == cached->second.lod)) lod = cached->second.lod;
        if (cached != meshes_.end() && cached->second.revision == wanted && cached->second.lod == lod) continue;
        const auto o = chunkOrigin(cand.pos);
        const float minY = cached != meshes_.end() ? cached->second.minY : -4.0f;
        const float maxY = cached != meshes_.end() ? cached->second.maxY : 30.0f;
        const glm::vec3 mn(static_cast<float>(o.x-origin.x), minY, static_cast<float>(o.y-origin.z));
        if (!frustum.intersects(mn, {mn.x+kChunkSize, maxY, mn.z+kChunkSize}) &&
            !(settings.shadows && cand.dist2 < (shadowRadius+24)*(shadowRadius+24))) continue;
        std::optional<Chunk> snapshot;
        std::set<u32> residential;
        if (active) {
            snapshot = *active;
            for (const auto& tile : active->tiles) if (tile.type == TileType::Building) {
                const auto* b = world.buildings.tryGet(BuildingId(tile.index));
                if (!b || b->info().category == BuildingCategory::Residential) residential.insert(tile.index);
            }
        }
        std::map<TilePos, bool> border;
        for (i32 n = -1; n <= kChunkSize; ++n) {
            for (const auto t : {TilePos{o.x+n,o.y-1}, TilePos{o.x+n,o.y+kChunkSize},
                                 TilePos{o.x-1,o.y+n}, TilePos{o.x+kChunkSize,o.y+n}}) border[t] = tileIsWater(map,t);
        }
        jobs.emplace(cand.pos.key(), std::async(std::launch::async,
            [snapshot = std::move(snapshot), residential = std::move(residential), border = std::move(border),
             gen = map.generator(), pos = cand.pos, lod]() mutable {
                if (!snapshot) { snapshot.emplace(); gen.generateChunk(pos, *snapshot); }
                return buildChunkGeometry(*snapshot, gen, [&](TilePos t) { return border.at(t); }, lod, nullptr,
                    [&](u32 id) { return residential.contains(id); });
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
        const auto lodFor = [](double d) { return d < 150.0 ? 0 : (d < 300.0 ? 1 : 2); };
        int lod = lodFor(dist);
        if (it != meshes_.end() && it->second.lod != lod &&
            (lodFor(dist + 16.0) == it->second.lod || lodFor(dist - 16.0) == it->second.lod))
            lod = it->second.lod;
        if ((it == meshes_.end() || it->second.revision != wanted || it->second.lod != lod) &&
            jobs.contains(cand.pos.key())) {
            ChunkMesh& mesh = meshes_[cand.pos.key()];
            const auto waitStart = std::chrono::steady_clock::now();
            const auto geometry = jobs.at(cand.pos.key()).get();
            stats_.meshWaitMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-waitStart).count();
            buildChunkMesh(world, cand.pos, lod, mesh, &geometry);
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

    // --- Per frame uniforms ---------------------------------------------------------------
    const glm::vec3 focus(static_cast<float>(cam.target.x - origin.x), static_cast<float>(cam.target.y),
                          static_cast<float>(cam.target.z - origin.z));
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
    fu.horizonColor = glm::vec4(sky_.horizon, 1.0f);
    fu.ambientSky = glm::vec4(sky_.ambientSky, 1.0f);
    fu.ambientGround = glm::vec4(sky_.ambientGround, 1.0f);
    const float fogEnd = static_cast<float>(viewRadius) - kChunkSize * 0.75f;
    fu.fogParams = glm::vec4(fogEnd * 0.5f, fogEnd, focus.x, focus.z);
    fu.shadowParams = glm::vec4(shadowTexel, 0.0006f, shadows ? 1.0f : 0.0f, 0.04f);
    glBindBuffer(GL_UNIFORM_BUFFER, frameUbo_.get());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameUniforms), &fu);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    collectVehicles(world, origin, cam.target, buildingRadius, tickFraction);

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
    stats_.trianglesDrawn += track_.draw(*terrainShader_, origin, cam.target, viewRadius, false);
    modelShader_->use();
    modelShader_->set("uOffset", glm::vec3(0.0f));
    dynamic_.draw(models_);
    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

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
