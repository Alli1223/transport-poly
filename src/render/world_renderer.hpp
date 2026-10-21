#pragma once
// Draws the world: sky, terrain chunks (streamed around the camera), roads, buildings, trees,
// water and overlays.

#include "core/types.hpp"
#include "render/camera.hpp"
#include "render/gl_util.hpp"
#include "render/mesh.hpp"
#include "render/track_renderer.hpp"
#include "render/vehicle_fx.hpp"
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
    bool weather = true;        // clouds, cloud shadows and mist
    float forceCloudCover = -1; // automation: fixed weather instead of the world's (when >= 0)
    float forceMist = -1;
    bool clouds = true;
    bool cloudShadows = true;
    float fogStrength = 1.0f;
    float nightFogStrength = 0.25f;
    float brightness = 1.0f;
    float treeDistance = 200.0f;
    float detailScale = 1.0f;
    bool particles = true;
    bool lightHalos = true;
    bool treeSway = true;
    bool waterAnimation = true;
};

// The sky's weather: it changes slowly from day to day, the same for every player of a world.
struct Weather {
    float cover = 0.0f;   // cloud cover, 0 = clear, 1 = overcast
    float mist = 0.0f;    // valley mist, strongest around dawn
    glm::vec2 wind{1, 0}; // direction the clouds drift
};
Weather computeWeather(u64 seed, double gameDays, double minuteOfDay);

struct RenderStats {
    int chunksVisible = 0;
    int chunksCached = 0;
    int meshesBuilt = 0;
    usize treesDrawn = 0;
    usize buildingsDrawn = 0;
    usize vehiclesDrawn = 0;
    usize peopleDrawn = 0;
    usize trianglesDrawn = 0;
    double meshWaitMs = 0;
    double meshCpuMs = 0;
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

// A lamp lighting the scene at night: all round, or a spot (headlights). Positions are relative to
// the render origin; colours already faded in by the night.
struct Lamp {
    glm::vec3 pos{0.0f};
    float reach = 1.0f;
    glm::vec3 colour{0.0f};
    glm::vec3 dir{0.0f, -1.0f, 0.0f};
    float cosCone = -2.0f; // below -1: all round
    float priority = 0.0f; // in tiles: a lamp this much nearer counts the same
};

// A soft halo drawn around a lamp.
struct Glow {
    glm::vec3 pos{0.0f};
    glm::vec3 colour{1.0f};
    float size = 0.2f;
    float strength = 1.0f;
};

inline constexpr int kMaxLamps = 24; // matches the shaders' light arrays

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
        glm::vec4 weather;
        glm::vec4 origin;
        glm::vec4 visualOptions;
        glm::vec4 lightPos[kMaxLamps];
        glm::vec4 lightColor[kMaxLamps];
        glm::vec4 lightDir[kMaxLamps];
        glm::vec4 lightCount;
    };

    struct DrawLists {
        std::vector<const ChunkMesh*> terrain;
        std::vector<const ChunkMesh*> trees;
        std::vector<const ChunkMesh*> buildings;
    };

    void buildChunkMesh(const World& world, ChunkPos pos, int lod, ChunkMesh& mesh, const ChunkGeometry* prepared = nullptr);
    void ensureShadowTarget();
    void drawChunks(const DrawLists& lists, const glm::dvec3& origin, bool shadowPass);
    void collectVehicles(const World& world, const glm::dvec3& origin, const glm::dvec3& focus, double radius,
                         double tickFraction);
    // Station platform, street and stop lamps near the focus (at night).
    void collectLamps(const World& world, const glm::dvec3& origin, const glm::dvec3& focus, double radius);
    void drawGlows(const glm::mat4& view);

    std::optional<Shader> terrainShader_, modelShader_, waterShader_, skyShader_, cloudShader_, glowShader_;
    std::vector<Lamp> lamps_;
    std::vector<Glow> glows_;
    VertexArray glowVao_;
    Buffer glowVbo_;
    VehicleFx fx_; // turning steam engine wheels, steam and smoke
    InstanceBatch clouds_;
    glm::vec2 cloudDrift_{0.0f};
    float mistLevel_ = 0.0f; // mist settles below about this height (eases after the camera)
    double lastSeconds_ = -1.0;
    Weather weather_;
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
