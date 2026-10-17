#pragma once

#include "render/camera.hpp"
#include "render/picking.hpp"
#include "render/world_renderer.hpp"
#include "sim/map.hpp"

#include <memory>
#include <optional>
#include <string>

struct GLFWwindow;

namespace tp {

class App {
public:
    int run(int argc, char** argv);

    // GLFW callback entry points.
    void onScroll(double dy);
    void onFramebufferSize(int width, int height);

private:
    bool init();
    void shutdown();
    void newWorld(u64 seed);
    void updateCamera(double dt);
    void drawDebugUi();
    bool parseArgs(int argc, char** argv);
    void captureScreenshot(const std::string& path);

    GLFWwindow* window_ = nullptr;
    gfx::WorldRenderer renderer_;
    gfx::Camera camera_;
    std::unique_ptr<Map> map_;
    gfx::TerrainHit hover_;

    double scrollAccum_ = 0.0;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool mouseInitialised_ = false;
    double minuteOfDay_ = 10.0 * 60.0;
    bool timeRunning_ = false;
    double timeSeconds_ = 0.0;
    float uiScale_ = 1.0f;
    u64 seed_ = 20261017;
    bool showDebug_ = true;

    // Command line automation: render a number of frames, save a screenshot and quit.
    std::optional<std::string> autoScreenshot_;
    int autoFrames_ = 90;
    std::optional<glm::dvec4> startCamera_; // x, z, distance, yaw (deg); pitch separately
    double startPitchDeg_ = -1.0;
    bool pendingScreenshot_ = false;
    bool gotoWater_ = false;
};

} // namespace tp
