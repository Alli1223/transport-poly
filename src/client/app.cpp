#include "client/app.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/calendar.hpp"
#include "core/log.hpp"
#include "core/png.hpp"
#include "core/version.hpp"
#include "render/gl.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>

namespace tp {

namespace {

void glfwErrorCallback(int code, const char* description) { log::error("GLFW error {}: {}", code, description); }

App* appFrom(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

// Finds a dry spot close to the origin to start the camera at.
TilePos findLandNear(const TerrainGenerator& gen, TilePos start) {
    for (i32 r = 0; r < 4000; r += 8) {
        for (i32 i = 0; i < 16; ++i) {
            const double a = i * 6.2831853 / 16.0;
            const TilePos p{start.x + static_cast<i32>(std::cos(a) * r), start.y + static_cast<i32>(std::sin(a) * r)};
            const int h = gen.cornerHeight(p.x, p.y);
            if (h >= 2 && h <= 8) return p;
        }
    }
    return start;
}

} // namespace

void App::onScroll(double dy) { scrollAccum_ += dy; }

bool App::parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--screenshot") {
            if (const char* v = next()) autoScreenshot_ = v;
        } else if (arg == "--frames") {
            if (const char* v = next()) autoFrames_ = std::max(1, std::atoi(v));
        } else if (arg == "--seed") {
            if (const char* v = next()) seed_ = std::strtoull(v, nullptr, 10);
        } else if (arg == "--time") {
            if (const char* v = next()) minuteOfDay_ = std::atof(v) * 60.0;
        } else if (arg == "--camera") {
            // x,z,distance,yawDegrees[,pitchDegrees]
            if (const char* v = next()) {
                double x = 0, z = 0, d = 70, yaw = 35, pitch = -1;
                const int n = std::sscanf(v, "%lf,%lf,%lf,%lf,%lf", &x, &z, &d, &yaw, &pitch);
                if (n >= 2) startCamera_ = glm::dvec4(x, z, d, yaw);
                if (n >= 5) startPitchDeg_ = pitch;
            }
        } else if (arg == "--goto-water") {
            gotoWater_ = true;
        } else if (arg == "--no-debug") {
            showDebug_ = false;
        } else {
            log::warn("Unknown argument '{}'", arg);
        }
    }
    return true;
}

void App::captureScreenshot(const std::string& path) {
    const int w = camera_.viewportWidth();
    const int h = camera_.viewportHeight();
    std::vector<u8> pixels(static_cast<usize>(w) * static_cast<usize>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    // OpenGL rows are bottom-up; PNG wants top-down.
    const usize row = static_cast<usize>(w) * 3;
    std::vector<u8> tmp(row);
    for (int y = 0; y < h / 2; ++y) {
        u8* a = pixels.data() + row * static_cast<usize>(y);
        u8* b = pixels.data() + row * static_cast<usize>(h - 1 - y);
        std::copy(a, a + row, tmp.data());
        std::copy(b, b + row, a);
        std::copy(tmp.data(), tmp.data() + row, b);
    }
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    if (writePng(path, w, h, 3, pixels))
        log::info("Saved screenshot {}", path);
    else
        log::error("Failed to save screenshot {}", path);
}

void App::onFramebufferSize(int width, int height) { camera_.setViewport(width, height); }

bool App::init() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    window_ = glfwCreateWindow(1600, 900, "transport-poly", nullptr, nullptr);
    if (!window_) return false;
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window_, this);
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double dy) { appFrom(w)->onScroll(dy); });
    glfwSetFramebufferSizeCallback(window_,
                                   [](GLFWwindow* w, int width, int height) { appFrom(w)->onFramebufferSize(width, height); });

    if (!gl::load(reinterpret_cast<gl::LoadProc>(glfwGetProcAddress))) return false;
    log::info("OpenGL {} on {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    glEnable(GL_MULTISAMPLE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";
    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(window_, &xs, &ys);
    uiScale_ = std::max(1.0f, xs);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(uiScale_);
    io.FontGlobalScale = uiScale_;
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (!renderer_.init()) {
        log::error("Renderer initialisation failed");
        return false;
    }
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    camera_.setViewport(fbw, fbh);
    newWorld(seed_);
    return true;
}

void App::newWorld(u64 seed) {
    seed_ = seed;
    map_ = std::make_unique<Map>(seed);
    renderer_.invalidateAll();
    const TilePos start = findLandNear(map_->generator(), {0, 0});
    camera_.targetGoal = glm::dvec3(start.x + 0.5, gfx::terrainHeightAt(*map_, start.x + 0.5, start.y + 0.5),
                                    start.y + 0.5);
    camera_.snapToGoals();
    // Activate a small area so the difference between simulated and generated chunks is testable.
    const ChunkPos c = chunkOf(start);
    for (i32 dy = -1; dy <= 1; ++dy)
        for (i32 dx = -1; dx <= 1; ++dx) map_->activate({c.x + dx, c.y + dy});
}

void App::shutdown() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void App::updateCamera(double dt) {
    const ImGuiIO& io = ImGui::GetIO();
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window_, &mx, &my);
    // Cursor coordinates are in window units; convert to framebuffer pixels for HiDPI.
    int ww = 1, wh = 1;
    glfwGetWindowSize(window_, &ww, &wh);
    const double sx = static_cast<double>(camera_.viewportWidth()) / std::max(ww, 1);
    const double sy = static_cast<double>(camera_.viewportHeight()) / std::max(wh, 1);
    if (!mouseInitialised_) {
        lastMouseX_ = mx;
        lastMouseY_ = my;
        mouseInitialised_ = true;
    }
    const double dx = mx - lastMouseX_;
    const double dy = my - lastMouseY_;
    lastMouseX_ = mx;
    lastMouseY_ = my;

    if (!io.WantCaptureKeyboard) {
        const double speed = camera_.distanceGoal * 1.1 * dt;
        const auto down = [&](int key) { return glfwGetKey(window_, key) == GLFW_PRESS; };
        double right = 0.0, forward = 0.0;
        if (down(GLFW_KEY_W) || down(GLFW_KEY_UP)) forward += speed;
        if (down(GLFW_KEY_S) || down(GLFW_KEY_DOWN)) forward -= speed;
        if (down(GLFW_KEY_D) || down(GLFW_KEY_RIGHT)) right += speed;
        if (down(GLFW_KEY_A) || down(GLFW_KEY_LEFT)) right -= speed;
        if (right != 0.0 || forward != 0.0) camera_.pan(right, forward);
        if (down(GLFW_KEY_Q)) camera_.orbit(-1.6 * dt, 0.0);
        if (down(GLFW_KEY_E)) camera_.orbit(1.6 * dt, 0.0);
        if (down(GLFW_KEY_R)) camera_.orbit(0.0, 1.0 * dt);
        if (down(GLFW_KEY_F)) camera_.orbit(0.0, -1.0 * dt);
        if (down(GLFW_KEY_PAGE_UP)) camera_.zoom(std::pow(0.3, dt));
        if (down(GLFW_KEY_PAGE_DOWN)) camera_.zoom(std::pow(3.0, dt));
    }
    if (!io.WantCaptureMouse) {
        if (scrollAccum_ != 0.0) camera_.zoom(std::pow(0.85, scrollAccum_));
        if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) camera_.orbit(dx * 0.006, dy * 0.004);
        if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            const double k = camera_.distance * 0.0016;
            camera_.pan(-dx * k, dy * k);
        }
        hover_ = gfx::pickTerrain(*map_, camera_.screenRay(mx * sx, my * sy));
    } else {
        hover_ = {};
    }
    scrollAccum_ = 0.0;

    // Keep the orbit target on the ground.
    camera_.targetGoal.y = gfx::terrainHeightAt(*map_, camera_.targetGoal.x, camera_.targetGoal.z);
    camera_.update(dt);
}

void App::drawDebugUi() {
    if (!showDebug_) return;
    const gfx::RenderStats& st = renderer_.stats();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug", &showDebug_, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("%s %s", kGameName.data(), kVersionString.data());
    ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("Camera %.1f, %.1f  dist %.0f", camera_.target.x, camera_.target.z, camera_.distance);
    ImGui::Text("Chunks: %d visible, %d cached, %d built", st.chunksVisible, st.chunksCached, st.meshesBuilt);
    ImGui::Text("Triangles: %zu  Trees: %zu", st.trianglesDrawn, st.treesDrawn);
    ImGui::Separator();
    float tod = static_cast<float>(minuteOfDay_ / 60.0);
    if (ImGui::SliderFloat("Time of day", &tod, 0.0f, 24.0f, "%.2f h")) minuteOfDay_ = tod * 60.0;
    ImGui::Checkbox("Run clock", &timeRunning_);
    ImGui::Checkbox("Shadows", &renderer_.settings.shadows);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &renderer_.settings.wireframe);
    ImGui::SliderFloat("View distance", &renderer_.settings.maxViewDistance, 120.0f, 900.0f, "%.0f");
    static int seedInput = 20261017;
    ImGui::InputInt("Seed", &seedInput);
    ImGui::SameLine();
    if (ImGui::Button("Generate")) newWorld(static_cast<u64>(static_cast<u32>(seedInput)));
    ImGui::Separator();
    if (hover_.hit) {
        const TileCorners c = map_->corners(hover_.tile);
        ImGui::Text("Tile (%d, %d)  corners %d %d %d %d", hover_.tile.x, hover_.tile.y, c.h[0], c.h[1], c.h[2], c.h[3]);
        if (const Tile* t = map_->tile(hover_.tile)) {
            ImGui::Text("Active: type %d ground %d trees %d", static_cast<int>(t->type), static_cast<int>(t->ground),
                        t->trees);
        } else {
            ImGui::TextUnformatted("Unexplored (generated preview)");
        }
    }
    ImGui::End();
}

int App::run(int argc, char** argv) {
    log::info("{} {} starting", kGameName, kVersionString);
    parseArgs(argc, argv);
    if (!init()) {
        shutdown();
        return EXIT_FAILURE;
    }
    if (gotoWater_) {
        // Jump to the nearest coastline: walk outwards until a sea corner is found.
        const TerrainGenerator& g = map_->generator();
        const TilePos s0{static_cast<i32>(camera_.target.x), static_cast<i32>(camera_.target.z)};
        bool found = false;
        for (i32 r = 0; r < 20000 && !found; r += 16) {
            for (i32 i = 0; i < 64 && !found; ++i) {
                const double a = i * 6.2831853 / 64.0;
                const TilePos p{s0.x + static_cast<i32>(std::cos(a) * r), s0.y + static_cast<i32>(std::sin(a) * r)};
                if (g.rawHeight(p.x, p.y) < -2 * kHeightOne) {
                    // Step back towards the start until we reach land: that is the coast.
                    for (i32 k = 0; k < r; k += 2) {
                        const TilePos q{p.x - static_cast<i32>(std::cos(a) * k), p.y - static_cast<i32>(std::sin(a) * k)};
                        if (g.rawHeight(q.x, q.y) > 0) {
                            startCamera_ = glm::dvec4(q.x, q.y, startCamera_ ? startCamera_->z : 70.0,
                                                      startCamera_ ? startCamera_->w : 35.0);
                            found = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (startCamera_) {
        const glm::dvec4 c = *startCamera_;
        camera_.targetGoal = glm::dvec3(c.x, gfx::terrainHeightAt(*map_, c.x, c.y), c.y);
        camera_.distanceGoal = c.z;
        camera_.yawGoal = glm::radians(c.w);
        if (startPitchDeg_ > 0.0) camera_.pitchGoal = glm::radians(startPitchDeg_);
        camera_.snapToGoals();
    }
    int frameCount = 0;
    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        const double now = glfwGetTime();
        const double dt = std::min(now - last, 0.1);
        last = now;
        timeSeconds_ += dt;
        if (timeRunning_) minuteOfDay_ = std::fmod(minuteOfDay_ + dt * 30.0, 1440.0);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_F3)) showDebug_ = !showDebug_;
        if (ImGui::IsKeyPressed(ImGuiKey_F12)) pendingScreenshot_ = true;
        updateCamera(dt);

        std::vector<gfx::OverlayQuad> overlays;
        if (hover_.hit) {
            const TileCorners c = map_->corners(hover_.tile);
            const double x = hover_.tile.x, z = hover_.tile.y, e = 0.02;
            overlays.push_back({{{x, c.h[0] * gfx::kHeightStep + e, z},
                                 {x + 1, c.h[1] * gfx::kHeightStep + e, z},
                                 {x + 1, c.h[2] * gfx::kHeightStep + e, z + 1},
                                 {x, c.h[3] * gfx::kHeightStep + e, z + 1}},
                                gfx::rgba(255, 255, 255, 90)});
        }

        drawDebugUi();
        renderer_.render(*map_, camera_, minuteOfDay_, timeSeconds_, overlays);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frameCount;
        if (pendingScreenshot_) {
            pendingScreenshot_ = false;
            const auto wall = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            captureScreenshot(std::format("screenshots/shot-{:%Y%m%d-%H%M%S}.png", wall));
        }
        if (autoScreenshot_ && frameCount >= autoFrames_) {
            captureScreenshot(*autoScreenshot_);
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        glfwSwapBuffers(window_);
    }
    shutdown();
    return EXIT_SUCCESS;
}

} // namespace tp
