#pragma once
// Vector icons drawn with ImDrawList primitives, so they stay crisp at any UI scale.

#include <imgui.h>

namespace tp::ui {

enum class Icon : unsigned char {
    None,
    // Simulation speed
    Pause,
    Play,
    Fast,
    Faster,
    // Tool groups and tools
    Landscape,
    Road,
    Bus,
    Tram,
    Train,
    Ship,
    Raise,
    Lower,
    Level,
    Smooth,
    Bulldoze,
    Tree,
    Track,
    Signal,
    Station,
    BusStop,
    Depot,
    Anchor,
    Shipyard,
    // Windows and information
    Coins,
    Town,
    News,
    Map,
    Calendar,
    Clock,
    Sun,
    Moon,
    People,
    Home,
    Briefcase,
    Cart,
    Walk,
    Car,
    Chart,
    Gauge,
    Star,
    // Actions
    Close,
    Target,
    Eye,
    Wrench,
    Clone,
    Link,
    Skip,
    Trash,
    Plus,
    Minus,
    ArrowUp,
    ArrowDown,
    Check,
    Menu,
    Save,
    Chat,
    Folder,
    Globe,
    Server,
    Power,
    Spark,
    Warning,
    Info,
    // Curved road tool
    RoadCurve,
    // Countryside industries (Tree marks a logging camp)
    Wheat,
    Sheep,
    Milk,
    Stone,
    MineCart,
    Pickaxe,
    Cheese,
    Spool,
    Saw,
    Bricks,
    Anvil,
    Factory,
    Chair,
    Fish,
};

// Draws `icon` centred on `centre`, fitting a `size` x `size` box.
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 centre, float size, ImU32 color);

// Places an icon inline in the current layout (like a text glyph of the current font size).
void iconInline(Icon icon, ImU32 color, float size = 0.0f);

} // namespace tp::ui
