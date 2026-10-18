#pragma once

#include "net/session.hpp"
#include "core/preferences.hpp"
#include "render/camera.hpp"
#include "sim/savegame.hpp"
#include "render/picking.hpp"
#include "render/minimap.hpp"
#include "render/world_renderer.hpp"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

struct GLFWwindow;

namespace tp {

enum class Screen { MainMenu, Playing };
enum class MenuPage { Main, NewGame, Load, Host, Join };

enum class Tool {
    None,
    Raise,
    Lower,
    Level,
    Smooth,
    Clear,
    Trees,
    Road,
    RemoveRoad,
    BusStop,
    RoadDepot,
    TramTrack,
    RemoveTram,
    TramStop,
    TramDepot,
    RailTrack,
    RemoveRail,
    Signal,
    BlockSignal,
    RailStation,
    RailDepot,
    Dock,
    ShipDepot,
    Canal,
    ReclaimWater,
    RoadBridge,
    RailBridge,
    RoadTunnel,
    RailTunnel,
    RemoveSpan,
    PickOrder, // click a station to append it to the selected vehicle's orders
};

// Toolbar categories; the tools of the open group are shown above the main toolbar.
enum class ToolGroup { None, Land, Road, Tram, Rail, Water };

class App {
public:
    int run(int argc, char** argv);

    // GLFW callback entry points.
    void onScroll(double dy);
    void onFramebufferSize(int width, int height);

private:
    // app.cpp
    bool init();
    void shutdown();
    void newGame(u64 seed);
    void updateCamera(double dt);
    void updateExploration(double now);
    void drawHud();
    void drawDebugUi();
    void drawToasts(double now);
    void drawTownLabels();
    void handleHotkeys();
    std::optional<glm::vec2> worldToScreen(const glm::dvec3& p) const;
    void processFeedback(double now);
    bool parseArgs(int argc, char** argv);
    void captureScreenshot(const std::string& path);
    World& world() { return session_->world(); }

    // app_tools.cpp
    void drawToolbar();
    void handleTools(std::vector<gfx::OverlayQuad>& overlays);
    void handleClick();
    void drawWorldOverlays(std::vector<gfx::OverlayQuad>& overlays);
    std::optional<CommandPayload> dragCommand(TilePos a, TilePos b) const;
    void addTileOverlay(std::vector<gfx::OverlayQuad>& overlays, TilePos t, u32 color, double lift = 0.04) const;
    VehicleId pickVehicle() const;

    // app_track.cpp
    void handleTrackTool(std::vector<gfx::OverlayQuad>& overlays);
    void handleRemoveTrackTool(std::vector<gfx::OverlayQuad>& overlays);

    // app_menu.cpp
    void drawMainMenu();
    void drawGameMenu();
    void drawConnecting();
    void drawChat();
    void startSession(std::unique_ptr<GameSession> session);
    void returnToMainMenu();
    void updatePreview(double dt);
    void autosave();
    void quicksave();
    void drawSettings();
    void applyPreferences();
    void placeCameraAtStart();

    // app_windows.cpp
    void drawWindows();
    void drawDepotWindow();
    void drawVehicleWindow();
    void drawStationWindow();
    void drawTownWindow();
    void drawFinanceWindow();
    void drawVehicleList();
    void drawNews(double now);
    void drawNewsWindow();
    void drawMinimap(double now);
    void drawTownList();
    void drawCitizens();
    void addCatchmentOverlay(std::vector<gfx::OverlayQuad>& overlays, const std::vector<TilePos>& tiles, i32 radius,
                             u32 color) const;
    std::string vehicleName(const Vehicle& v) const;
    std::string vehicleStatus(const Vehicle& v) const;
    void focusOn(TilePos t);

    GLFWwindow* window_ = nullptr;
    Screen screen_ = Screen::MainMenu;
    MenuPage menuPage_ = MenuPage::Main;
    std::unique_ptr<GameSession> preview_; // background world on the main menu
    std::vector<SaveInfo> saves_;
    std::string menuError_;
    std::string exeDir_;
    char seedBuf_[32] = "";
    char playerName_[48] = "Player";
    char companyName_[64] = "";
    char address_[128] = "127.0.0.1";
    char saveName_[64] = "my-game";
    char chatBuf_[256] = "";
    int port_ = 3979;
    bool showGameMenu_ = false;
    bool showSettings_ = false;
    ClientPreferences preferences_;
    std::string settingsError_;
    bool showChat_ = false;
    bool showClients_ = false;
    bool pickViaOrder_ = false;
    bool cameraPlaced_ = false;
    std::deque<std::string> chatLines_;
    i32 lastAutosaveMonth_ = -1;
    gfx::WorldRenderer renderer_;
    gfx::Camera camera_;
    std::unique_ptr<GameSession> session_;
    gfx::TerrainHit hover_;
    gfx::Ray mouseRay_;
    bool mouseOverUi_ = false;
    bool escConsumed_ = false; // Escape already handled this frame (tool, flyout, windows, menu)
    float dockHeight_ = 0.0f;  // height of the bottom dock, for placing things above it
    Tool tool_ = Tool::None;
    ToolGroup toolGroup_ = ToolGroup::None;
    i32 brushRadius_ = 3;      // terraform brush size, tiles
    double lastBrush_ = 0.0;   // when the brush last applied (held mouse repeats)
    u16 levelTarget_ = 0;      // fine height the Level brush flattens to
    std::optional<TilePos> dragStart_;
    std::optional<TrackAnchor> trackStart_; // free-form track being laid (start of the next piece)

    // Open windows / selection.
    DepotId selDepot_;
    VehicleId selVehicle_;
    StationId selStation_;
    TownId selTown_;
    VehicleId followVehicle_;
    bool showFinances_ = false;
    bool showVehicleList_ = false;
    bool showNews_ = false;
    bool showMinimap_ = true;
    int overlayMode_ = 0; // none, catchments, passenger flow, town zones
    bool showTownList_ = false;
    bool showCitizens_ = false;
    TownId citizenTown_;
    PersonId selPerson_;
    u32 selPersonSerial_ = 0;
    bool followPerson_ = false;
    int fleetCompany_ = -2;
    std::deque<std::pair<i64, std::string>> citizenDiary_;

    // News: the newest item already announced, and the banner currently shown.
    const void* newsSession_ = nullptr;
    i64 lastNewsTick_ = -1;
    std::optional<NewsItem> newsBanner_;
    double newsBannerUntil_ = 0.0;

    gfx::Minimap minimap_;
    double minimapUpdated_ = -1.0;
    glm::dvec2 minimapCentre_{0.0};
    i32 minimapScale_ = 2;
    // Commands whose result should open a window (sequence number -> kind).
    u32 pendingBuySeq_ = 0;

    // Exploration requests in flight: chunk key -> time requested.
    std::unordered_map<u64, double> explorePending_;
    double lastExplore_ = 0.0;

    struct Toast {
        std::string text;
        double expires;
        bool error;
        double born = 0.0; // set when first shown
        float x = -1.0f;   // screen position for floating cost labels, < 0 for cards
        float y = -1.0f;
    };
    std::deque<Toast> toasts_;

    double scrollAccum_ = 0.0;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool mouseInitialised_ = false;
    double timeSeconds_ = 0.0;
    float uiScale_ = 1.0f;
    u64 seed_ = 20261017;
    bool showDebug_ = false; // F3
    std::optional<double> fixedTimeOfDay_; // overrides the game clock for screenshots
    bool daylightOnly_ = false; // local lighting preference; simulation clock keeps running

    // Command line automation: render a number of frames, save a screenshot and quit.
    std::optional<std::string> autoScreenshot_;
    int autoFrames_ = 90;
    std::optional<glm::dvec4> startCamera_; // x, z, distance, yaw (deg)
    double startPitchDeg_ = -1.0;
    bool pendingScreenshot_ = false;
    bool gotoWater_ = false;
    bool gotoMountain_ = false; // automation: start near high ground
    double gotoTownDistance_ = 0.0; // > 0: jump to the biggest town at this zoom (automation)
    bool demoNetwork_ = false;      // automation: build a demo bus line in the biggest town
    bool demoRail_ = false;         // automation: build a demo railway next to the biggest town
    bool demoShips_ = false;        // automation: build a demo ferry line on the nearest coast
    bool demoTrack_ = false;        // automation: lay curved free-form track near the camera
    bool followFirst_ = false;      // automation: follow the first vehicle
    bool directStart_ = false;      // skip the main menu (command line options given)
    std::string joinAddress_;       // --join host[:port]
    std::string openWindows_;       // automation: --open depot,station,town,finances,vehicles,towns,news
};

} // namespace tp
