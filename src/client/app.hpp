#pragma once

#include "net/session.hpp"
#include "net/host_session.hpp"
#include "net/net_client.hpp"
#include "client/updater.hpp"
#include "core/preferences.hpp"
#include "audio/audio.hpp"
#include "render/camera.hpp"
#include "sim/savegame.hpp"
#include "render/picking.hpp"
#include "render/minimap.hpp"
#include "render/terrain_preview.hpp"
#include "render/world_renderer.hpp"

#include <deque>
#include <functional>
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
    RoadTunnel,
    RemoveSpan,
    StationEntrance,
    Electrify,   // put up overhead wires over track
    RemoveWires, // take them down
    DemolishBuilding,
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

    // app_inspect.cpp
    VehicleId pickVehicle(float* screenDistance = nullptr) const;
    PersonId pickPerson(float* screenDistance = nullptr) const;
    void updateHover();
    void selectPerson(PersonId id);
    void drawInspectOverlays(std::vector<gfx::OverlayQuad>& overlays);
    void drawPersonRoute(std::vector<gfx::OverlayQuad>& overlays);
    void drawPersonLabels();
    void drawPersonWindow();
    void drawVehiclePassengers(const Vehicle& v);

    // app_multiplayer.cpp
    net::NetClientSession* netClient();
    net::HostSession* netHost();
    std::vector<net::ClientInfo> players();
    u16 localClientId();
    bool companyLocked(CompanyId company);
    bool colourPicker(const char* id, int& colour, CompanyId own);
    void drawCompanyLobby();
    void drawPlayersWindow();
    void drawPlayerCursors(std::vector<gfx::OverlayQuad>& overlays);
    void updateBots(double dt);

    // app_update.cpp
    bool updatesConfigured() const;
    void startUpdateCheck();
    void drawUpdatePanel();
    void drawUpdateSettings();

    // app_companies.cpp
    void drawCompaniesWindow();
    void applyCompetition(WorldSettings& ws) const;
    void competitionPicker();

    // app_feedback.cpp
    void drawFeedbackWindow();
    void drawFeedbackEntries(const std::function<bool(const Feedback&)>& match, usize max);
    void satisfactionMeter(const char* label, i64 sum, i64 count);
    void drawSubsidiesWindow();

    // app_lines.cpp
    void drawLinesWindow();
    void drawLineEditor();
    // A button summing up which kinds of freight a stop picks up or drops off; it opens a list to
    // choose them. True when the choice changed.
    bool cargoChoice(const char* id, const char* label, u32& mask, bool enabled);
    // Picking stops on the map adds to the selected vehicle's orders or, while the line editor
    // asks for it, to the line's.
    bool pickTarget(std::vector<Order>& orders, VehicleType& type);
    CommandResult testPickedOrders(const std::vector<Order>& orders);
    void commitPickedOrders(const std::vector<Order>& orders);
    void drawLineRoutes(std::vector<gfx::OverlayQuad>& overlays, bool all);

    // app_track.cpp
    void handleTrackTool(std::vector<gfx::OverlayQuad>& overlays);
    void handleRemoveTrackTool(std::vector<gfx::OverlayQuad>& overlays);
    void handleStationTool(std::vector<gfx::OverlayQuad>& overlays);
    void handleDepotTool(std::vector<gfx::OverlayQuad>& overlays);
    void handleSignalTool(std::vector<gfx::OverlayQuad>& overlays);
    // Signals on free-form track: the blocks they divide it into (coloured overlay), what one
    // signal does (the blocks either side and its directions), and the panel to change it.
    struct SignalBlocks {
        const World* world = nullptr;
        u32 revision = 0;
        std::unordered_map<u32, u32> block; // edge -> block
        std::unordered_map<u32, u8> colour; // block -> palette entry
    };
    struct SignalView {
        struct Side {
            bool valid = false;
            u32 block = 0;
            i64 length = 0;     // units
            VehicleId heldBy;   // a train holding part of the block
        } sides[2];
    };
    const SignalBlocks& currentSignalBlocks();
    void drawSignalBlocks(std::vector<gfx::OverlayQuad>& overlays, TrackNodeId focus = {});
    bool nearCamera(const TrackEdge& e) const;
    SignalView signalView(TrackNodeId node);
    void highlightSignal(std::vector<gfx::OverlayQuad>& overlays, TrackNodeId node);
    std::string signalSummary(TrackNodeId node);
    void drawSignalWindow();
    void previewSignalling(TPoint lineStart); // automation for --preview-signals and --preview-branch
    void handleElectrifyTool(std::vector<gfx::OverlayQuad>& overlays);
    bool pickTrackOrder(std::vector<gfx::OverlayQuad>& overlays);
    void drawStationUpgradePreview(std::vector<gfx::OverlayQuad>& overlays);
    bool elevationTool() const; // a tool that shows the elevation view
    void updateElevationView(double dt);

    // app_menu.cpp
    void drawMainMenu();
    void drawGameMenu();
    void drawConnecting();
    void drawChat();
    void startSession(std::unique_ptr<GameSession> session);
    void returnToMainMenu();
    void applyScreenTransition();
    void updatePreview(double dt);
    void autosave();
    void quicksave();
    void drawSettings();
    void applyPreferences();
    void updateAudio(double now);
    void placeCameraAtStart();

    // app_windows.cpp
    void drawWindows();
    void drawDepotWindow();
    void drawVehicleCatalogue(DepotId depot);
    void drawVehicleWindow();
    void drawStationWindow();
    void drawTownWindow();
    void drawIndustryWindow();
    void drawTownNeeds(const Town& t);
    void drawFinanceWindow();
    void drawVehicleList();
    void drawReplaceWindow();
    void drawOrderTimetable(VehicleId vehicle, usize index, Order& o, bool& changed);
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
    int windowedX_ = 100, windowedY_ = 100, windowedWidth_ = 1600, windowedHeight_ = 900;
    int startupAntiAliasing_ = 2;
    bool wasMaximized_ = false;
    void applyDisplayMode();
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
    VehicleId consistVehicle_;
    int requestedCatalogueTab_ = -1;
    u16 consistCar_ = 0;
    bool showGameMenu_ = false;
    bool showSettings_ = false;
    bool openAudioSettings_ = false;
    ClientPreferences preferences_;
    Audio audio_;
    double nextAudioUpdate_ = 0;
    bool returnToMenuRequested_ = false;
    int menuReturnFrame_ = -1; // automation: exercise leaving a live session mid-frame
    std::string settingsError_;
    bool showChat_ = false;
    bool showClients_ = false;
    bool pickViaOrder_ = false;
    // The line editor: a line being made (line 0, its stops kept here until it is created) or one
    // being changed (its stops are the world's).
    struct LineEditor {
        u32 line = 0;
        VehicleType type = VehicleType::Rail;
        char name[64] = {};
        std::vector<Order> orders;
        u32 awaiting = 0; // asked to create the line: it is the company's first line above this id
        VehicleId lead;   // ... or the line this vehicle ends up running
    };
    std::optional<LineEditor> lineEditor_;
    bool pickForLine_ = false;
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
    StationId stationEditTarget_;
    i32 stationPlatformLength_ = 6;
    // The station tool's choices: what a new station is for, how many platforms it has, and a
    // fixed platform length in tiles (0: as far as the pointer is dragged).
    StationUse stationUse_ = StationUse::Passengers;
    int stationPlatforms_ = 1;
    i32 stationFixedLength_ = 0;
    std::optional<CommandPayload> stationUpgradePreview_; // hovered station action, cleared every frame
    std::string stationUpgradeLabel_;
    i32 brushRadius_ = 3;      // terraform brush size, tiles
    double lastBrush_ = 0.0;   // when the brush last applied (held mouse repeats)
    u16 levelTarget_ = 0;      // fine height the Level brush flattens to
    std::optional<TilePos> dragStart_;
    std::optional<TrackAnchor> trackStart_; // free-form track being laid (start of the next piece)
    std::optional<TrackAnchor> stationStart_; // first end of a station platform being placed
    TrackNodeId selSignal_;          // the signal open in the signal panel
    bool newSignalOneWay_ = false;   // the signal tool puts in one-way signals
    SignalBlocks signalBlocks_;
    bool elevationView_ = false;     // the elevation view turned on (H) outside the track tools
    float elevationStrength_ = 0.0f; // fades the view in and out

    // Open windows / selection.
    DepotId selDepot_;
    VehicleId selVehicle_;
    StationId selStation_;
    TownId selTown_;
    BuildingId selIndustry_;
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
    bool showPersonCard_ = false; // the clicked person's card and route
    VehicleId hoverVehicle_;      // under the cursor
    PersonId hoverPerson_;
    bool showLines_ = false;
    // Multiplayer: the company lobby, chat to a company or a player, other players' pointers.
    bool showLobby_ = false;
    // Updates: the release feed (set by release builds, or --update-feed) and what the player chose.
    update::Updater updater_;
    std::string updateFeed_;
    bool updateDismissed_ = false;
    bool showUpdateErrors_ = false; // only after the player asked to check
    bool autoUpdate_ = false;        // automation: install an update as soon as it is found
    bool updateAnnounced_ = false;
    // Companies window: league, graphs and ratings; the competition chosen for a new game.
    bool showCompanies_ = false;
    int companiesTab_ = 0;
    int graphMetric_ = 0;
    int graphSpan_ = 1;
    CompanyId ratingCompany_;
    int competition_ = 0;
    int fastForwardDays_ = 0; // automation: run a hosted demo game this many days before showing it
    std::string lobbyNotice_;
    int joinTarget_ = -1; // company whose password is being asked for
    char joinPassword_[64] = "";
    char lobbyCompanyName_[64] = "";
    int companyColour_ = -1; // chosen livery for a new company (-1: the first free one)
    char companyPassword_[64] = "";
    char serverPassword_[64] = "";
    int chatScope_ = 0; // net::ChatScope
    u16 chatTarget_ = 0;
    std::unordered_map<u16, glm::dvec3> cursorDisplay_;
    std::vector<std::unique_ptr<net::NetClientSession>> bots_; // automation: --host-demo
    bool botsGreeted_ = false;
    int hostDemoBots_ = 0;
    std::vector<u32> wiredThisDrag_; // edges already sent while dragging the electrify tool
    u32 selLine_ = 0;                         // key of the line shown in the lines window
    std::shared_ptr<struct LineCache> lineCache_; // routes of the lines drawn on the map
    int fleetCompany_ = -2;
    bool showReplace_ = false; // vehicle replacement: autorenew and autoreplace rules
    bool showFeedback_ = false;
    int feedbackFilter_ = 0; // all, complaints, praise
    bool showSubsidies_ = false;
    int replaceType_ = 0;      // VehicleType shown in the replacement window
    std::deque<std::pair<i64, std::string>> citizenDiary_;

    // News: the newest item already announced, and the banner currently shown.
    const void* newsSession_ = nullptr;
    i64 lastNewsTick_ = -1;
    std::optional<NewsItem> newsBanner_;
    double newsBannerUntil_ = 0.0;

    gfx::Minimap minimap_;
    gfx::TerrainPreview terrainPreview_; // the new game screen's picture of the land
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
    std::string profilePath_;
    std::optional<glm::dvec4> startCamera_; // x, z, distance, yaw (deg)
    double startPitchDeg_ = -1.0;
    bool pendingScreenshot_ = false;
    bool gotoWater_ = false;
    bool gotoMountain_ = false; // automation: start near high ground
    std::optional<std::string> gotoIndustry_; // automation: look at the nearest industry (whose name contains this)
    bool demoIndustries_ = false;            // automation: found one of every industry near the camera
    bool demoFreight_ = false;               // automation: build a freight station at the camera
    // Command line choices (terrain, competitors, ground detail), applied over the saved preferences.
    std::vector<std::function<void(ClientPreferences&)>> preferenceArgs_;
    double gotoTownDistance_ = 0.0; // > 0: jump to the biggest town at this zoom (automation)
    bool demoNetwork_ = false;      // automation: build a demo bus line in the biggest town
    bool demoRail_ = false;         // automation: build a demo railway next to the biggest town
    bool demoShips_ = false;        // automation: build a demo ferry line on the nearest coast
    bool demoTrack_ = false;        // automation: lay curved free-form track near the camera
    bool demoStation_ = false;      // automation: road entrances, walking and station upgrades
    bool previewStationDetails_ = false;
    bool previewPassengers_ = false; // automation: occupied coaches and tram with intact roofs
    bool previewDemolition_ = false;
    bool previewStation_ = false;   // automation: show the station tool placing a platform
    bool previewTrack_ = false;     // automation: show the track tool laying track, in the elevation view
    bool previewSignals_ = false;   // automation: the demo railway with signals, their blocks and one open
    bool previewBranch_ = false;    // automation: the demo railway with track branching off part way along
    std::string previewStationUpgradeAction_; // automation: left, right, west or east
    bool previewConsist_ = false;
    bool previewConsistPopup_ = false;
    bool previewOrders_ = false;    // automation: show the order picker over a platform
    std::optional<TrackStructure> demoStructure_; // automation: build a line needing a bridge or tunnel
    bool demoCrossing_ = false;     // automation: build a line across a road
    std::optional<glm::dvec3> previewHover_; // automation: scripted cursor position on the ground
    bool followFirst_ = false;      // automation: follow the first vehicle
    bool inspectFirst_ = false;     // automation: open the card of someone travelling by public transport
    std::string demoCars_;          // automation: cars (comma separated names) for the first train
    bool demoElectric_ = false;     // automation: electrify the demo railway
    bool directStart_ = false;      // skip the main menu (command line options given)
    std::string joinAddress_;       // --join host[:port]
    std::string openWindows_;       // automation: --open depot,station,town,finances,vehicles,towns,news
};

} // namespace tp
