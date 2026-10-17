#include "render/world_renderer.hpp"

#include "core/log.hpp"
#include "render/shaders.hpp"

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
    if (const Tile* tile = map.tile(t)) return tile->type == TileType::Water;
    const TerrainGenerator& g = map.generator();
    return g.rawHeight(t.x, t.y) < 0 && g.rawHeight(t.x + 1, t.y) < 0 && g.rawHeight(t.x + 1, t.y + 1) < 0 &&
           g.rawHeight(t.x, t.y + 1) < 0;
}

ModelId treeModel(const TreePlacement& t) {
    if (t.snowy) return ModelId::TreeConiferSnow;
    const int base = static_cast<int>(t.type == TreeType::Conifer ? ModelId::TreeConifer0 : ModelId::TreeDeciduous0);
    return static_cast<ModelId>(base + t.variant % 3);
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

void WorldRenderer::buildChunkMesh(const Map& map, ChunkPos pos, int lod, ChunkMesh& mesh) {
    const Chunk* source = map.chunk(pos);
    Chunk generated;
    if (!source) {
        map.generator().generateChunk(pos, generated);
        source = &generated;
    }
    const ChunkGeometry geo =
        buildChunkGeometry(*source, map.generator(), [&](TilePos t) { return tileIsWater(map, t); }, lod);
    mesh.pos = pos;
    mesh.lod = lod;
    mesh.ground.upload(geo.ground);
    mesh.water.upload(geo.water);
    mesh.minY = geo.minY;
    mesh.maxY = geo.maxY;

    std::array<std::vector<ModelInstance>, kModelCount> perModel;
    for (const TreePlacement& t : geo.trees)
        perModel[static_cast<usize>(treeModel(t))].push_back(makeInstance(t.pos, t.yaw, t.scale));
    std::vector<ModelInstance> all;
    all.reserve(geo.trees.size());
    for (usize i = 0; i < kModelCount; ++i) {
        mesh.treeOffset[i] = static_cast<u32>(all.size());
        mesh.treeCount[i] = static_cast<u32>(perModel[i].size());
        all.insert(all.end(), perModel[i].begin(), perModel[i].end());
    }
    mesh.treeTotal = all.size();
    if (!all.empty()) {
        if (!mesh.treeBuffer) mesh.treeBuffer = makeBuffer();
        glBindBuffer(GL_ARRAY_BUFFER, mesh.treeBuffer.get());
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(all.size() * sizeof(ModelInstance)), all.data(),
                     GL_STATIC_DRAW);
    }
    const Chunk* active = map.chunk(pos);
    mesh.revision = active ? active->revision + 1 : 0;
}

void WorldRenderer::drawChunks(const std::vector<const ChunkMesh*>& chunks,
                               const std::vector<const ChunkMesh*>& treeChunks, const glm::dvec3& origin,
                               bool shadowPass) {
    const auto offsetOf = [&](const ChunkMesh* c) {
        const TilePos o = chunkOrigin(c->pos);
        return glm::vec3(static_cast<float>(o.x - origin.x), 0.0f, static_cast<float>(o.y - origin.z));
    };
    const Shader& ts = shadowPass ? *shadowStaticShader_ : *terrainShader_;
    ts.use();
    for (const ChunkMesh* c : chunks) {
        ts.set("uOffset", offsetOf(c));
        c->ground.draw();
        if (!shadowPass) stats_.trianglesDrawn += static_cast<usize>(c->ground.vertexCount() / 3);
    }
    const Shader& ms = shadowPass ? *shadowModelShader_ : *modelShader_;
    ms.use();
    for (const ChunkMesh* c : treeChunks) {
        if (c->treeTotal == 0) continue;
        ms.set("uOffset", offsetOf(c));
        for (usize i = 0; i < kModelCount; ++i) {
            if (c->treeCount[i] == 0) continue;
            models_.drawInstanced(static_cast<ModelId>(i), c->treeBuffer.get(), c->treeOffset[i] * sizeof(ModelInstance),
                                  static_cast<GLsizei>(c->treeCount[i]));
        }
        if (!shadowPass) stats_.treesDrawn += c->treeTotal;
    }
}

void WorldRenderer::render(const Map& map, const Camera& cam, double minuteOfDay, double timeSeconds,
                           const std::vector<OverlayQuad>& overlays) {
    ++frame_;
    stats_ = {};
    const glm::dvec3 origin(std::floor(cam.target.x), 0.0, std::floor(cam.target.z));
    const glm::mat4 view = cam.viewMatrix(origin);
    const glm::mat4 proj = cam.projectionMatrix();
    const glm::mat4 viewProj = proj * view;
    sky_ = computeSky(minuteOfDay);

    // --- Determine visible chunks, building meshes nearest first -------------------------
    const double viewRadius = std::min<double>(settings.maxViewDistance, cam.distance * 2.6 + 120.0);
    const double shadowRadius = std::min(viewRadius, cam.distance * 1.1 + 24.0);
    const double treeRadius = std::min(viewRadius, 200.0);
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

    std::vector<const ChunkMesh*> visible, casters, treeChunks, treeCasters;
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
            builds < settings.meshBuildsPerFrame) {
            ChunkMesh& mesh = meshes_[cand.pos.key()];
            buildChunkMesh(map, cand.pos, lod, mesh);
            ++builds;
            it = meshes_.find(cand.pos.key());
        }
        if (it == meshes_.end()) continue;
        ChunkMesh& mesh = it->second;
        mesh.lastUsedFrame = frame_;
        const bool trees = cand.dist2 < treeRadius * treeRadius;
        if (inFrustum) {
            visible.push_back(&mesh);
            if (trees) treeChunks.push_back(&mesh);
        }
        if (caster) {
            casters.push_back(&mesh);
            if (trees) treeCasters.push_back(&mesh);
        }
    }
    stats_.meshesBuilt = builds;
    stats_.chunksVisible = static_cast<int>(visible.size());
    if (frame_ % 60 == 0) {
        std::erase_if(meshes_, [&](const auto& kv) { return kv.second.lastUsedFrame + 180 < frame_; });
    }
    stats_.chunksCached = static_cast<int>(meshes_.size());

    // --- Per frame uniforms ---------------------------------------------------------------
    const glm::vec3 focus(static_cast<float>(cam.target.x - origin.x), static_cast<float>(cam.target.y),
                          static_cast<float>(cam.target.z - origin.z));
    const bool shadows = settings.shadows && sky_.sunStrength > 0.01f;
    glm::mat4 lightViewProj(1.0f);
    float shadowTexel = 1.0f / static_cast<float>(shadowSize_);
    if (shadows) {
        const float radius = std::ceil(static_cast<float>(shadowRadius) / 8.0f) * 8.0f;
        const glm::vec3 up = std::abs(sky_.sunDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 lightView = glm::lookAt(focus + sky_.sunDir * 300.0f, focus, up);
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 1.0f, 700.0f);
        // Snap to shadow texels to avoid shimmering while the camera moves.
        glm::vec4 o = lightProj * lightView * glm::vec4(0, 0, 0, 1);
        const float half = static_cast<float>(shadowSize_) * 0.5f;
        o *= half;
        const glm::vec2 snapped(std::round(o.x), std::round(o.y));
        lightProj[3][0] += (snapped.x - o.x) / half;
        lightProj[3][1] += (snapped.y - o.y) / half;
        lightViewProj = lightProj * lightView;
    }

    FrameUniforms fu{};
    fu.viewProj = viewProj;
    fu.lightViewProj = lightViewProj;
    const glm::dvec3 eye = cam.eye() - origin;
    fu.cameraPos = glm::vec4(glm::vec3(eye), static_cast<float>(timeSeconds));
    fu.sunDir = glm::vec4(sky_.sunDir, sky_.sunStrength);
    fu.sunColor = glm::vec4(sky_.sunColor, 1.0f);
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
        drawChunks(casters, treeCasters, origin, true);
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
    drawChunks(visible, treeChunks, origin, false);
    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Water (translucent)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    waterShader_->use();
    for (const ChunkMesh* c : visible) {
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
        MeshBuilder mb;
        for (const OverlayQuad& q : overlays) {
            glm::vec3 p[4];
            for (int i = 0; i < 4; ++i) p[i] = glm::vec3(q.corners[i] - origin);
            mb.quad(p[0], p[1], p[2], p[3], q.color);
        }
        overlayMesh_.upload(mb.vertices);
        overlayShader_->use();
        overlayShader_->set("uOffset", glm::vec3(0.0f));
        glDisable(GL_CULL_FACE);
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
