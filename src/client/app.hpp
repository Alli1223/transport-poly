#pragma once

#include "net/session.hpp"
#include "render/camera.hpp"
#include "render/picking.hpp"
#include "render/world_renderer.hpp"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

struct GLFWwindow;

namespace tp {

enum class Tool { None, Raise, Lower, Clear, Trees };

class App {
public:
    int run(int argc, char** argv);

    // GLFW callback entry points.
    void onScroll(double dy);
    void onFramebufferSize(int width, int height);

private:
    bool init();
    void shutdown();
    void newGame(u64 seed);
    void updateCamera(double dt);
    void updateExploration(double now);
    void handleTools();
    void drawHud();
    void drawToolbar();
    void drawDebugUi();
    void drawToasts(double now);
    void drawTownLabels();
    std::optional<glm::vec2> worldToScreen(const glm::dvec3& p) const;
    void processFeedback(double now);
    bool parseArgs(int argc, char** argv);
    void captureScreenshot(const std::string& path);
    World& world() { return session_->world(); }

    GLFWwindow* window_ = nullptr;
    gfx::WorldRenderer renderer_;
    gfx::Camera camera_;
    std::unique_ptr<GameSession> session_;
    gfx::TerrainHit hover_;
    Tool tool_ = Tool::None;

    // Exploration requests in flight: chunk key -> time requested.
    std::unordered_map<u64, double> explorePending_;
    double lastExplore_ = 0.0;

    struct Toast {
        std::string text;
        double expires;
        bool error;
    };
    std::deque<Toast> toasts_;

    double scrollAccum_ = 0.0;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool mouseInitialised_ = false;
    double timeSeconds_ = 0.0;
    float uiScale_ = 1.0f;
    u64 seed_ = 20261017;
    bool showDebug_ = true;
    std::optional<double> fixedTimeOfDay_; // overrides the game clock for screenshots

    // Command line automation: render a number of frames, save a screenshot and quit.
    std::optional<std::string> autoScreenshot_;
    int autoFrames_ = 90;
    std::optional<glm::dvec4> startCamera_; // x, z, distance, yaw (deg)
    double startPitchDeg_ = -1.0;
    bool pendingScreenshot_ = false;
    bool gotoWater_ = false;
    double gotoTownDistance_ = 0.0; // > 0: jump to the biggest town at this zoom (automation)
};

} // namespace tp
