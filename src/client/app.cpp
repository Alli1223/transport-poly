#include "client/app.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/calendar.hpp"
#include "core/log.hpp"
#include "core/png.hpp"
#include "net/net_client.hpp"
#include "core/math.hpp"
#include "core/version.hpp"
#include "render/gl.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/road.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>

namespace tp {

namespace {

void glfwErrorCallback(int code, const char* description) { log::error("GLFW error {}: {}", code, description); }

App* appFrom(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

// Finds a dry spot close to `start` for the camera.
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

std::optional<TilePos> findCoastNear(const TerrainGenerator& g, TilePos s0) {
    for (i32 r = 0; r < 20000; r += 16) {
        for (i32 i = 0; i < 64; ++i) {
            const double a = i * 6.2831853 / 64.0;
            const TilePos p{s0.x + static_cast<i32>(std::cos(a) * r), s0.y + static_cast<i32>(std::sin(a) * r)};
            if (g.rawHeight(p.x, p.y) >= -2 * kHeightOne) continue;
            for (i32 k = 0; k < r; k += 2) {
                const TilePos q{p.x - static_cast<i32>(std::cos(a) * k), p.y - static_cast<i32>(std::sin(a) * k)};
                if (g.rawHeight(q.x, q.y) > 0) return q;
            }
        }
    }
    return std::nullopt;
}

// Builds a small bus line in the biggest town (used for automated screenshots / smoke tests).
void buildDemoNetwork(World& w, CompanyId company) {
    const Town* town = nullptr;
    for (const auto [id, t] : w.towns)
        if (!town || t.population > town->population) town = &t;
    if (!town) return;
    const auto run = [&](CommandPayload p) {
        Command c;
        c.company = company;
        c.payload = std::move(p);
        return executeCommand(w, c);
    };
    std::vector<TilePos> stops;
    for (const TilePos r : town->roads) {
        const Tile* t = w.map.tile(r);
        if (!t || t->type != TileType::Road || !isStraight(roadPieces(*t)) || !w.map.slope(r).isFlat()) continue;
        bool farEnough = true;
        for (const TilePos s : stops) farEnough = farEnough && manhattan(s.x - r.x, s.y - r.y) >= 9;
        if (farEnough && run(CmdBuildRoadStop{r, false}).ok) stops.push_back(r);
        if (stops.size() >= 4) break;
    }
    if (stops.size() < 2) return;
    DepotId depot;
    for (const TilePos r : town->roads) {
        for (const Dir d : kDirs) {
            const CommandResult res = run(CmdBuildRoadDepot{neighbour(r, d), opposite(d), false});
            if (res.ok) {
                depot = DepotId(res.createdId);
                break;
            }
        }
        if (depot.valid()) break;
    }
    if (!depot.valid()) return;
    std::vector<Order> orders;
    for (const TilePos s : stops) {
        Order o;
        o.station = StationId(w.map.tile(s)->index);
        orders.push_back(o);
    }
    for (int i = 0; i < 4; ++i) {
        const CommandResult buy = run(CmdBuyVehicle{depot, 0});
        if (!buy.ok) break;
        const VehicleId v(buy.createdId);
        std::rotate(orders.begin(), orders.begin() + 1, orders.end());
        run(CmdSetOrders{v, orders});
        run(CmdStartStopVehicle{v});
    }
}

} // namespace

void App::onScroll(double dy) { scrollAccum_ += dy; }

void App::onFramebufferSize(int width, int height) { camera_.setViewport(width, height); }

bool App::parseArgs(int argc, char** argv) {
    if (argc > 0 && argv[0]) exeDir_ = std::filesystem::path(argv[0]).parent_path().string();
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg != "--no-debug" && arg != "--screenshot" && arg != "--frames" && arg != "--time") directStart_ = true;
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--screenshot") {
            if (const char* v = next()) autoScreenshot_ = v;
        } else if (arg == "--frames") {
            if (const char* v = next()) autoFrames_ = std::max(1, std::atoi(v));
        } else if (arg == "--seed") {
            if (const char* v = next()) seed_ = std::strtoull(v, nullptr, 10);
        } else if (arg == "--time") {
            if (const char* v = next()) fixedTimeOfDay_ = std::atof(v) * 60.0;
        } else if (arg == "--camera") {
            // x,z,distance,yawDegrees[,pitchDegrees]
            if (const char* v = next()) {
                double x = 0, z = 0, d = 70, yaw = 35, pitch = -1;
                const int n = std::sscanf(v, "%lf,%lf,%lf,%lf,%lf", &x, &z, &d, &yaw, &pitch);
                if (n >= 2) startCamera_ = glm::dvec4(x, z, d, yaw);
                if (n >= 5) startPitchDeg_ = pitch;
            }
        } else if (arg == "--goto-town") {
            if (const char* v = next()) gotoTownDistance_ = std::max(6.0, std::atof(v));
        } else if (arg == "--goto-water") {
            gotoWater_ = true;
        } else if (arg == "--follow") {
            followFirst_ = true;
        } else if (arg == "--demo-network") {
            demoNetwork_ = true;
        } else if (arg == "--join") {
            if (const char* v = next()) joinAddress_ = v;
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

bool App::init() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    if (autoScreenshot_) {
        // Automated runs must not steal keyboard focus from whatever the user is doing.
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    window_ = glfwCreateWindow(1600, 900, "transport-poly", nullptr, nullptr);
    if (!window_) return false;
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window_, this);
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double dy) { appFrom(w)->onScroll(dy); });
    glfwSetFramebufferSizeCallback(
        window_, [](GLFWwindow* w, int width, int height) { appFrom(w)->onFramebufferSize(width, height); });

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
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScaleAllSizes(uiScale_);
    // Prefer the bundled Roboto font, fall back to the built-in one.
    bool fontLoaded = false;
    for (const std::filesystem::path dir : {std::filesystem::path(exeDir_) / "fonts", std::filesystem::path("fonts")}) {
        const std::filesystem::path font = dir / "Roboto-Medium.ttf";
        std::error_code ec;
        if (!fontLoaded && std::filesystem::exists(font, ec)) {
            fontLoaded = io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16.0f * uiScale_) != nullptr;
        }
    }
    if (!fontLoaded) io.FontGlobalScale = uiScale_;
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (!renderer_.init()) {
        log::error("Renderer initialisation failed");
        return false;
    }
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    camera_.setViewport(fbw, fbh);
    if (!joinAddress_.empty()) {
        std::string host = joinAddress_;
        u16 port = net::kDefaultPort;
        if (const auto colon = host.rfind(':'); colon != std::string::npos) {
            port = static_cast<u16>(std::atoi(host.substr(colon + 1).c_str()));
            host = host.substr(0, colon);
        }
        startSession(std::make_unique<net::NetClientSession>(host, port, "Player"));
    } else if (directStart_) {
        newGame(seed_);
    }
    return true;
}

void App::newGame(u64 seed) {
    seed_ = seed;
    WorldSettings settings;
    settings.seed = seed;
    startSession(std::make_unique<LocalSession>(settings));
}

void App::shutdown() {
    session_.reset();
    preview_.reset();
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void App::updateCamera(double dt) {
    if (const Vehicle* v = world().vehicles.tryGet(followVehicle_)) {
        if (const auto pose = gfx::vehiclePose(world(), *v, session_->tickFraction())) camera_.targetGoal = pose->pos;
    } else {
        followVehicle_ = {};
    }
    if (autoScreenshot_) {
        // Ignore all input during automated screenshots.
        hover_ = {};
        mouseOverUi_ = true;
        camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
        camera_.update(dt);
        return;
    }
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
        if (right != 0.0 || forward != 0.0) {
            camera_.pan(right, forward);
            followVehicle_ = {};
        }
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
        mouseRay_ = camera_.screenRay(mx * sx, my * sy);
        hover_ = gfx::pickTerrain(world().map, mouseRay_);
    } else {
        hover_ = {};
    }
    mouseOverUi_ = io.WantCaptureMouse;
    scrollAccum_ = 0.0;

    // Keep the orbit target on the ground.
    camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
    camera_.update(dt);
}

void App::updateExploration(double now) {
    if (now - lastExplore_ < 0.15) return;
    lastExplore_ = now;
    const Map& map = world().map;
    std::erase_if(explorePending_, [&](const auto& kv) {
        return map.isActive(ChunkPos::fromKey(kv.first)) || now - kv.second > 3.0;
    });
    const double radius = std::clamp(camera_.distance * 1.5, 128.0, 260.0);
    const double cx = camera_.target.x, cz = camera_.target.z;
    struct Want {
        ChunkPos pos;
        double d2;
    };
    std::vector<Want> wanted;
    const i32 x0 = static_cast<i32>(std::floor((cx - radius) / kChunkSize));
    const i32 x1 = static_cast<i32>(std::floor((cx + radius) / kChunkSize));
    const i32 z0 = static_cast<i32>(std::floor((cz - radius) / kChunkSize));
    const i32 z1 = static_cast<i32>(std::floor((cz + radius) / kChunkSize));
    for (i32 z = z0; z <= z1; ++z) {
        for (i32 x = x0; x <= x1; ++x) {
            const ChunkPos p{x, z};
            if (map.isActive(p) || explorePending_.contains(p.key())) continue;
            const double dx = (x + 0.5) * kChunkSize - cx, dz = (z + 0.5) * kChunkSize - cz;
            const double d2 = dx * dx + dz * dz;
            if (d2 > radius * radius) continue;
            wanted.push_back({p, d2});
        }
    }
    if (wanted.empty()) return;
    std::sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.d2 < b.d2; });
    CmdActivateChunks cmd;
    for (const Want& w : wanted) {
        if (cmd.chunks.size() >= 32) break;
        cmd.chunks.push_back(w.pos);
        explorePending_[w.pos.key()] = now;
    }
    session_->submit(std::move(cmd));
}

void App::processFeedback(double now) {
    for (const CommandFeedback& f : session_->takeFeedback()) {
        if (f.result.ok) {
            if (f.seq == pendingBuySeq_ && f.result.createdId != 0xFFFFFFFFu) {
                selVehicle_ = VehicleId(f.result.createdId);
                pendingBuySeq_ = 0;
            }
            if (f.result.cost > 0) toasts_.push_back({std::format("-{}", formatMoney(f.result.cost)), now + 2.0, false});
            else if (f.result.cost < 0)
                toasts_.push_back({std::format("+{}", formatMoney(-f.result.cost)), now + 2.0, false});
        } else if (f.name != "Explore") {
            toasts_.push_back({std::format("{}: {}", f.name, f.result.error), now + 4.0, true});
        }
    }
    while (toasts_.size() > 6) toasts_.pop_front();
}

void App::drawToasts(double now) {
    std::erase_if(toasts_, [&](const Toast& t) { return t.expires < now; });
    if (toasts_.empty()) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 60.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##toasts", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    for (const Toast& t : toasts_)
        ImGui::TextColored(t.error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "%s",
                           t.text.c_str());
    ImGui::End();
}

std::optional<glm::vec2> App::worldToScreen(const glm::dvec3& p) const {
    const glm::dvec3 origin = camera_.target;
    const glm::dvec4 clip = camera_.viewProjectionD(origin) * glm::dvec4(p - origin, 1.0);
    if (clip.w <= 0.01) return std::nullopt;
    const glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
    if (ndc.z > 1.0 || ndc.x < -1.2 || ndc.x > 1.2 || ndc.y < -1.2 || ndc.y > 1.2) return std::nullopt;
    const ImGuiIO& io = ImGui::GetIO();
    return glm::vec2(static_cast<float>((ndc.x + 1.0) * 0.5 * io.DisplaySize.x),
                     static_cast<float>((1.0 - ndc.y) * 0.5 * io.DisplaySize.y));
}

void App::drawTownLabels() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const double maxDist = camera_.distance * 3.0 + 150.0;
    for (const auto [id, t] : world().towns) {
        const double dx = t.centre.x + 0.5 - camera_.target.x;
        const double dz = t.centre.y + 0.5 - camera_.target.z;
        if (dx * dx + dz * dz > maxDist * maxDist) continue;
        const double y = world().map.cornerHeight(t.centre.x, t.centre.y) * gfx::kHeightStep + 2.5;
        const auto sp = worldToScreen({t.centre.x + 0.5, y, t.centre.y + 0.5});
        if (!sp) continue;
        const std::string label = std::format("{}  {}", t.name, t.population);
        const float scale = t.cls == TownClass::City ? 1.25f : (t.cls == TownClass::Town ? 1.1f : 1.0f);
        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize() * scale;
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
        const ImVec2 pos(sp->x - ts.x * 0.5f, sp->y - ts.y * 0.5f);
        dl->AddRectFilled(ImVec2(pos.x - 6, pos.y - 3), ImVec2(pos.x + ts.x + 6, pos.y + ts.y + 3),
                          IM_COL32(20, 24, 30, 150), 4.0f);
        dl->AddText(font, size, pos, IM_COL32(255, 255, 255, 235), label.c_str());
    }
}

void App::drawHud() {
    World& w = world();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 6.0f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const Company* co = w.companies.tryGet(session_->localCompany());
    ImGui::Text("%s  %s", formatDate(w.clock.day()).c_str(), formatTimeOfDay(w.clock.minuteOfDay()).c_str());
    ImGui::SameLine(0, 24);
    if (co) {
        ImGui::TextColored(co->money < 0 ? ImVec4(1, 0.4f, 0.4f, 1) : ImVec4(0.6f, 1, 0.6f, 1), "%s",
                           formatMoney(co->money).c_str());
        ImGui::SameLine(0, 24);
        ImGui::TextUnformatted(co->name.c_str());
        ImGui::SameLine(0, 24);
    }
    const int speeds[] = {0, 1, 2, 4, 8, 16};
    const char* labels[] = {"||", "1x", "2x", "4x", "8x", "16x"};
    for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        const bool active = session_->speed() == speeds[i];
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(labels[i])) session_->setSpeed(speeds[i]); // network clients ignore this
        if (active) ImGui::PopStyleColor();
    }
    ImGui::End();
}

void App::drawDebugUi() {
    if (!showDebug_) return;
    const gfx::RenderStats& st = renderer_.stats();
    World& w = world();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug", &showDebug_, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("%s %s", kGameName.data(), kVersionString.data());
    ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("Camera %.1f, %.1f  dist %.0f", camera_.target.x, camera_.target.z, camera_.distance);
    ImGui::Text("Chunks: %d visible, %d cached, %d built", st.chunksVisible, st.chunksCached, st.meshesBuilt);
    ImGui::Text("Triangles: %zu  Trees: %zu  Buildings: %zu", st.trianglesDrawn, st.treesDrawn, st.buildingsDrawn);
    ImGui::Text("Towns %zu  people %zu  buildings %zu", w.towns.size(), w.people.size(), w.buildings.size());
    ImGui::Text("Tick %lld  explored chunks %zu", static_cast<long long>(w.clock.tick), w.map.activeChunks().size());
    ImGui::Text("State hash %016llx", static_cast<unsigned long long>(w.stateHash()));
    ImGui::Separator();
    ImGui::Checkbox("Shadows", &renderer_.settings.shadows);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &renderer_.settings.wireframe);
    ImGui::SliderFloat("View distance", &renderer_.settings.maxViewDistance, 120.0f, 900.0f, "%.0f");
    static int seedInput = 20261017;
    ImGui::InputInt("Seed", &seedInput);
    ImGui::SameLine();
    if (ImGui::Button("New game")) newGame(static_cast<u64>(static_cast<u32>(seedInput)));
    ImGui::Separator();
    if (hover_.hit) {
        const TileCorners c = w.map.corners(hover_.tile);
        ImGui::Text("Tile (%d, %d)  corners %d %d %d %d", hover_.tile.x, hover_.tile.y, c.h[0], c.h[1], c.h[2], c.h[3]);
        if (const Tile* t = w.map.tile(hover_.tile)) {
            ImGui::Text("type %d ground %d trees %d road %x", static_cast<int>(t->type), static_cast<int>(t->ground),
                        t->trees, t->road);
            if (t->type == TileType::Building) {
                if (const Building* b = w.buildings.tryGet(BuildingId(t->index))) {
                    const Town* town = w.towns.tryGet(b->town);
                    ImGui::Text("%s (%s)", b->info().name.data(), town ? town->name.c_str() : "?");
                    ImGui::Text("residents %d  workers %d/%d  customers %d", b->residents, b->workers, b->info().jobs,
                                b->customers);
                }
            }
        } else {
            ImGui::TextUnformatted("Unexplored");
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
        const TilePos s0{static_cast<i32>(camera_.target.x), static_cast<i32>(camera_.target.z)};
        if (const auto coast = findCoastNear(world().map.generator(), s0))
            startCamera_ = glm::dvec4(coast->x, coast->y, startCamera_ ? startCamera_->z : 70.0,
                                      startCamera_ ? startCamera_->w : 35.0);
    }
    if (startCamera_) {
        const glm::dvec4 c = *startCamera_;
        camera_.targetGoal = glm::dvec3(c.x, gfx::terrainHeightAt(world().map, c.x, c.y), c.y);
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

        if (session_) {
            session_->update(dt);
            if (session_->ready()) processFeedback(now);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (ImGui::IsKeyPressed(ImGuiKey_F12)) pendingScreenshot_ = true;

        if (screen_ == Screen::MainMenu || !session_) {
            updatePreview(dt);
            drawMainMenu();
            if (preview_) renderer_.render(preview_->world(), camera_, 15.5 * 60.0, timeSeconds_, preview_->tickFraction(), {});
        } else if (!session_->ready()) {
            glViewport(0, 0, camera_.viewportWidth(), camera_.viewportHeight());
            glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawConnecting();
        } else {
            if (!cameraPlaced_) placeCameraAtStart();
            if (preview_) {
                preview_.reset();
                renderer_.invalidateAll();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F3)) showDebug_ = !showDebug_;
            if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space) && !session_->isMultiplayer())
                session_->setSpeed(session_->speed() == 0 ? 1 : 0);
            if (gotoTownDistance_ > 0.0 && frameCount == 90) {
                // Jump to the most populous town found so far (used for automated screenshots).
                const Town* best = nullptr;
                for (const auto [id, t] : world().towns)
                    if (!best || t.population > best->population) best = &t;
                if (best) {
                    camera_.targetGoal = glm::dvec3(best->centre.x + 0.5, 0.0, best->centre.y + 0.5);
                    camera_.targetGoal.y =
                        gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                    camera_.distanceGoal = gotoTownDistance_;
                    camera_.snapToGoals();
                }
            }
            if (demoNetwork_ && frameCount == 100) {
                if (auto* local = dynamic_cast<LocalSession*>(session_.get()))
                    buildDemoNetwork(local->world(), local->localCompany());
            }
            if (followFirst_ && frameCount == 160) {
                for (const auto [id, v] : world().vehicles) {
                    if (v.state == VehicleState::InDepot) continue;
                    followVehicle_ = id;
                    selVehicle_ = id;
                    break;
                }
            }
            updateCamera(dt);
            updateExploration(now);
            autosave();

            std::vector<gfx::OverlayQuad> overlays;
            if (hover_.hit && tool_ == Tool::None)
                addTileOverlay(overlays, hover_.tile, gfx::rgba(255, 255, 255, 70), 0.04);
            handleTools(overlays);

            drawHud();
            drawToolbar();
            drawWindows();
            drawDebugUi();
            drawChat();
            drawGameMenu();
            drawToasts(now);
            const double minute =
                fixedTimeOfDay_ ? *fixedTimeOfDay_ : world().clock.exactMinuteOfDay(session_->tickFraction());
            drawTownLabels();
            renderer_.render(world(), camera_, minute, timeSeconds_, session_->tickFraction(), overlays);
        }
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
