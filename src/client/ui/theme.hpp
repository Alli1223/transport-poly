#pragma once
// Visual theme of the client UI: colour palette, fonts and the Dear ImGui style.

#include <imgui.h>

#include <filesystem>

namespace tp::ui {

// Palette. Surfaces are a cool slate; the amber accent is the colour of a signal lamp.
namespace col {
inline constexpr ImU32 kWindow = IM_COL32(21, 25, 32, 244);
inline constexpr ImU32 kBar = IM_COL32(18, 21, 28, 236);
inline constexpr ImU32 kCard = IM_COL32(31, 36, 46, 255);
inline constexpr ImU32 kCardHover = IM_COL32(39, 45, 58, 255);
inline constexpr ImU32 kFrame = IM_COL32(38, 44, 56, 255);
inline constexpr ImU32 kFrameHover = IM_COL32(49, 56, 71, 255);
inline constexpr ImU32 kFrameActive = IM_COL32(58, 66, 84, 255);
inline constexpr ImU32 kBorder = IM_COL32(255, 255, 255, 20);
inline constexpr ImU32 kBorderStrong = IM_COL32(255, 255, 255, 38);
inline constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 255);

inline constexpr ImU32 kText = IM_COL32(234, 238, 244, 255);
inline constexpr ImU32 kTextMuted = IM_COL32(146, 155, 172, 255);
inline constexpr ImU32 kTextDim = IM_COL32(101, 110, 127, 255);
inline constexpr ImU32 kTextOnAccent = IM_COL32(28, 22, 12, 255);

inline constexpr ImU32 kAccent = IM_COL32(255, 186, 66, 255);
inline constexpr ImU32 kBlue = IM_COL32(88, 166, 255, 255);
inline constexpr ImU32 kGreen = IM_COL32(92, 214, 144, 255);
inline constexpr ImU32 kRed = IM_COL32(255, 108, 108, 255);
inline constexpr ImU32 kOrange = IM_COL32(255, 158, 72, 255);
inline constexpr ImU32 kPurple = IM_COL32(196, 140, 255, 255);
inline constexpr ImU32 kTeal = IM_COL32(72, 206, 218, 255);
inline constexpr ImU32 kLeaf = IM_COL32(132, 204, 98, 255);

// Transport modes / tool groups.
inline constexpr ImU32 kLand = kLeaf;
inline constexpr ImU32 kRoad = kOrange;
inline constexpr ImU32 kTram = kPurple;
inline constexpr ImU32 kRail = kBlue;
inline constexpr ImU32 kWater = kTeal;
} // namespace col

// Colour helpers.
ImVec4 vec(ImU32 c);
ImU32 withAlpha(ImU32 c, float alpha);
ImU32 mix(ImU32 a, ImU32 b, float t);
ImU32 rgb(unsigned rgb24, float alpha = 1.0f); // 0xRRGGBB

struct Fonts {
    ImFont* body = nullptr;    // regular UI text
    ImFont* small = nullptr;   // captions, labels, badges
    ImFont* heading = nullptr; // panel titles, big numbers
    ImFont* title = nullptr;   // main menu title
};
Fonts& fonts();

// Loads the UI fonts (falls back to Dear ImGui's built-in font when the file is missing) and
// applies the style. Call once after creating the ImGui context.
void setup(const std::filesystem::path& fontFile, float scale);
float scale();

// RAII font push.
class FontScope {
public:
    explicit FontScope(ImFont* f) : pushed_(f != nullptr) {
        if (pushed_) ImGui::PushFont(f);
    }
    ~FontScope() {
        if (pushed_) ImGui::PopFont();
    }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;

private:
    bool pushed_;
};

} // namespace tp::ui
