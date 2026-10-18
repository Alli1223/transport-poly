#pragma once
// Draws the world: sky, terrain chunks (streamed around the camera), roads, buildings, trees,
// water and overlays.

#include "core/types.hpp"
#include "render/camera.hpp"
#include "render/gl_util.hpp"
#include "render/mesh.hpp"
#include "render/track_renderer.hpp"
#include "render/models.hpp"
#include "render/shader.hpp"
#include "render/terrain_mesher.hpp"
#include "sim/world.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

namespace tp::gfx {

struct RenderSettings {
    bool shadows = true;
    int shadowMapSize = 2048;
    float maxViewDistance = 460.0f; // tiles
    int meshBuildsPerFrame = 6;
    bool wireframe = false;
};

struct RenderStats {
    int chunksVisible = 0;
    int chunksCached = 0;
    int meshesBuilt = 0;
    usize treesDrawn = 0;
    usize buildingsDrawn = 0;
    usize vehiclesDrawn = 0;
    usize peopleDrawn = 0;
    usize trianglesDrawn = 0;
};

// Lighting and sky colours (linear) for a time of day.
struct SkyState {
    glm::vec3 sunDir{0, 1, 0};
    float sunStrength = 1.0f;
    glm::vec3 sunColor{1.0f};
    glm::vec3 zenith{0.3f, 0.5f, 0.9f};
    glm::vec3 horizon{0.7f, 0.8f, 0.9f};
    glm::vec3 ambientSky{0.4f};
    glm::vec3 ambientGround{0.3f};
    float night = 0.0f; // 0 = day, 1 = full night
};

SkyState computeSky(double minuteOfDay);

// Translucent quad drawn over the terrain (selection, previews). Corners in world coordinates.
struct OverlayQuad {
    glm::dvec3 corners[4];
    u32 color;
    bool xray = false; // also drawn faintly where the ground hides it (build previews)
};

class WorldRenderer {
public:
    bool init();
    void render(const World& world, const Camera& camera, double minuteOfDay, double timeSeconds,
                double tickFraction, const std::vector<OverlayQuad>& overlays);

    RenderSettings settings;
    const RenderStats& stats() const { return stats_; }
    const SkyState& sky() const { return sky_; }
    void invalidateAll() {
        meshes_.clear();
        track_.clear();
    }
    const ModelLibrary& models() const { return models_; }

private:
    struct ChunkMesh {
        ChunkPos pos;
        u32 revision = 0;
        int lod = 0;
        bool active = false;
        GpuMesh ground;
        GpuMesh water;
        StaticInstances trees;
        StaticInstances buildings;
        float minY = -4.0f;
        float maxY = 30.0f;
        u64 lastUsedFrame = 0;
    };

    struct FrameUniforms {
        glm::mat4 viewProj;
        glm::mat4 lightViewProj;
        glm::vec4 cameraPos;
        glm::vec4 sunDir;
        glm::vec4 sunColor;
        glm::vec4 zenithColor;
        glm::vec4 horizonColor;
        glm::vec4 ambientSky;
        glm::vec4 ambientGround;
        glm::vec4 fogParams;
        glm::vec4 shadowParams;
    };

    struct DrawLists {
        std::vector<const ChunkMesh*> terrain;
        std::vector<const ChunkMesh*> trees;
        std::vector<const ChunkMesh*> buildings;
    };

    void buildChunkMesh(const World& world, ChunkPos pos, int lod, ChunkMesh& mesh);
    void ensureShadowTarget();
    void drawChunks(const DrawLists& lists, const glm::dvec3& origin, bool shadowPass);
    void collectVehicles(const World& world, const glm::dvec3& origin, const glm::dvec3& focus, double radius,
                         double tickFraction);

    std::optional<Shader> terrainShader_, modelShader_, waterShader_, skyShader_;
    std::optional<Shader> shadowStaticShader_, shadowModelShader_, overlayShader_;
    ModelLibrary models_;
    Buffer frameUbo_;
    VertexArray emptyVao_;
    Framebuffer shadowFbo_;
    Texture shadowTex_;
    int shadowSize_ = 0;
    GpuMesh overlayMesh_;
    InstanceBatch dynamic_; // vehicles and other moving things, rebuilt every frame
    TrackRenderer track_;

    std::unordered_map<u64, ChunkMesh> meshes_;
    u64 frame_ = 0;
    RenderStats stats_;
    SkyState sky_;
};

} // namespace tp::gfx
