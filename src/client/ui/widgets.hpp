#pragma once
// Custom Dear ImGui widgets used throughout the client: panels with a styled header, icon and
// tool buttons, rich tooltips, statistics cards, meters, badges, charts and world labels.

#include "client/ui/icons.hpp"
#include "client/ui/theme.hpp"
#include "core/money.hpp"

#include <span>

namespace tp::ui {

// Value easing towards `target`, remembered per ID in the current window.
float animate(ImGuiID id, float target, float speed = 14.0f);

// Soft drop shadow around a rectangle.
void dropShadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread = 16.0f, float alpha = 0.32f);

struct Tip {
    const char* title = nullptr;
    const char* body = nullptr;
    const char* hotkey = nullptr;
};
void tooltip(const Tip& tip);     // shows a rich tooltip right away
void itemTooltip(const Tip& tip); // ... when the last item is hovered (after the usual delay)
void keyCap(const char* key);     // inline keyboard key

// Square icon button; `active` highlights it in the accent colour.
bool iconButton(const char* id, Icon icon, float size, bool active = false, ImU32 accent = col::kAccent,
                const Tip& tip = {});
// Small borderless icon button for inline actions (order lists, close buttons).
bool miniButton(const char* id, Icon icon, ImU32 color = col::kTextMuted, const char* tip = nullptr);
// Construction tool: icon above a short label. `removeBadge` adds a red minus.
bool toolTile(const char* id, Icon icon, const char* label, bool active, ImU32 accent, const Tip& tip,
              bool removeBadge = false);

enum class Btn { Primary, Secondary, Danger, Ghost };
// Text button with an optional icon. size.x < 0 stretches to the available width.
bool button(const char* label, Icon icon = Icon::None, Btn kind = Btn::Secondary, ImVec2 size = ImVec2(0, 0));

struct Segment {
    const char* label = nullptr;
    Icon icon = Icon::None;
    const char* tip = nullptr;
};
// Row of joined toggle buttons; returns the index clicked this frame or -1.
int segmented(const char* id, std::span<const Segment> items, int current, float height = 0.0f);

// Floating panel with an icon, title, optional subtitle and close button. Width is fixed; height
// follows the contents unless given. Always pair with endPanel(), whatever beginPanel returned.
struct PanelSpec {
    ImVec2 pos{100, 100};
    ImVec2 pivot{0, 0};
    ImGuiCond cond = ImGuiCond_FirstUseEver;
    float width = 360.0f; // unscaled pixels
    float height = 0.0f;  // 0 = fit contents
    ImU32 accent = col::kAccent;
    const char* subtitle = nullptr;
    ImGuiWindowFlags flags = 0;
};
bool beginPanel(const char* id, const char* title, Icon icon, bool* open, const PanelSpec& spec);
void endPanel();

// Undecorated floating bar (HUD, dock) with rounded background and shadow.
void beginBar(const char* id, ImVec2 pos, ImVec2 pivot, ImVec2 padding = ImVec2(10, 8), float rounding = 16.0f);
void endBar();

// Layout and data display.
float columnWidth(int columns); // width of one of `columns` equal columns in the remaining space
void sectionHeader(const char* text);
void statRow(const char* label, const char* value, ImU32 valueColor = col::kText);
void moneyRow(const char* label, Money value);
void meter(float fraction, ImU32 color, float height = 0.0f);
void meterRow(const char* label, const char* value, float fraction, ImU32 color);
void badge(const char* text, ImU32 color);
void statCard(Icon icon, const char* value, const char* label, ImU32 color, float width);
void stackedBar(std::span<const float> fractions, std::span<const ImU32> colors, float height = 0.0f);
void legendItem(ImU32 color, const char* label);
void sparkline(std::span<const float> values, ImVec2 size, ImU32 color);
void spinner(float radius, ImU32 color);
void emptyState(Icon icon, const char* text);
void iconText(Icon icon, ImU32 iconColor, const char* text, ImU32 textColor = col::kText);
void mutedText(const char* text);
// Label above an input that spans the panel width.
void fieldLabel(const char* text);

// Name tag floating over the world (towns, stations).
void worldLabel(ImDrawList* dl, ImVec2 centre, const char* title, const char* detail, ImU32 dot, float scale = 1.0f);
// A larger floating label for things seen from afar (countryside industries): a badge holding an
// icon, the title in the heading font and, when given, a detail line under it.
void iconWorldLabel(ImDrawList* dl, ImVec2 centre, Icon icon, ImU32 badge, const char* title, const char* detail,
                    float scale = 1.0f);

} // namespace tp::ui
