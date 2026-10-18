#include "client/ui/theme.hpp"

#include <algorithm>
#include <system_error>

namespace tp::ui {

namespace {

Fonts gFonts;
float gScale = 1.0f;

void applyStyle(float s) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.Alpha = 1.0f;
    st.DisabledAlpha = 0.45f;
    st.WindowPadding = ImVec2(14, 12);
    st.FramePadding = ImVec2(10, 6);
    st.CellPadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 7);
    st.ItemInnerSpacing = ImVec2(6, 5);
    st.IndentSpacing = 18;
    st.ScrollbarSize = 11;
    st.GrabMinSize = 10;
    st.WindowBorderSize = 1;
    st.ChildBorderSize = 0;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 0;
    st.TabBorderSize = 0;
    st.WindowRounding = 12;
    st.ChildRounding = 8;
    st.FrameRounding = 7;
    st.PopupRounding = 10;
    st.ScrollbarRounding = 8;
    st.GrabRounding = 6;
    st.TabRounding = 7;
    st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.SeparatorTextBorderSize = 1;
    st.SeparatorTextPadding = ImVec2(0, 4);
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.HoverDelayShort = 0.25f;
    st.HoverDelayNormal = 0.5f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = vec(col::kText);
    c[ImGuiCol_TextDisabled] = vec(col::kTextDim);
    c[ImGuiCol_WindowBg] = vec(col::kWindow);
    c[ImGuiCol_ChildBg] = vec(IM_COL32(0, 0, 0, 0));
    c[ImGuiCol_PopupBg] = vec(IM_COL32(26, 30, 39, 250));
    c[ImGuiCol_Border] = vec(col::kBorder);
    c[ImGuiCol_BorderShadow] = vec(IM_COL32(0, 0, 0, 0));
    c[ImGuiCol_FrameBg] = vec(col::kFrame);
    c[ImGuiCol_FrameBgHovered] = vec(col::kFrameHover);
    c[ImGuiCol_FrameBgActive] = vec(col::kFrameActive);
    c[ImGuiCol_TitleBg] = vec(col::kWindow);
    c[ImGuiCol_TitleBgActive] = vec(col::kWindow);
    c[ImGuiCol_TitleBgCollapsed] = vec(col::kWindow);
    c[ImGuiCol_MenuBarBg] = vec(col::kCard);
    c[ImGuiCol_ScrollbarBg] = vec(IM_COL32(0, 0, 0, 0));
    c[ImGuiCol_ScrollbarGrab] = vec(col::kFrameHover);
    c[ImGuiCol_ScrollbarGrabHovered] = vec(col::kFrameActive);
    c[ImGuiCol_ScrollbarGrabActive] = vec(withAlpha(col::kAccent, 0.7f));
    c[ImGuiCol_CheckMark] = vec(col::kAccent);
    c[ImGuiCol_SliderGrab] = vec(col::kAccent);
    c[ImGuiCol_SliderGrabActive] = vec(mix(col::kAccent, IM_COL32(255, 255, 255, 255), 0.3f));
    c[ImGuiCol_Button] = vec(col::kFrame);
    c[ImGuiCol_ButtonHovered] = vec(col::kFrameHover);
    c[ImGuiCol_ButtonActive] = vec(col::kFrameActive);
    c[ImGuiCol_Header] = vec(withAlpha(col::kBlue, 0.22f));
    c[ImGuiCol_HeaderHovered] = vec(withAlpha(col::kBlue, 0.30f));
    c[ImGuiCol_HeaderActive] = vec(withAlpha(col::kBlue, 0.40f));
    c[ImGuiCol_Separator] = vec(col::kBorder);
    c[ImGuiCol_SeparatorHovered] = vec(withAlpha(col::kAccent, 0.5f));
    c[ImGuiCol_SeparatorActive] = vec(col::kAccent);
    c[ImGuiCol_ResizeGrip] = vec(IM_COL32(0, 0, 0, 0));
    c[ImGuiCol_ResizeGripHovered] = vec(withAlpha(col::kAccent, 0.4f));
    c[ImGuiCol_ResizeGripActive] = vec(withAlpha(col::kAccent, 0.7f));
    c[ImGuiCol_Tab] = vec(col::kFrame);
    c[ImGuiCol_TabHovered] = vec(col::kFrameHover);
    c[ImGuiCol_TabSelected] = vec(col::kFrameActive);
    c[ImGuiCol_TableHeaderBg] = vec(col::kCard);
    c[ImGuiCol_TableBorderStrong] = vec(col::kBorder);
    c[ImGuiCol_TableBorderLight] = vec(col::kBorder);
    c[ImGuiCol_TableRowBg] = vec(IM_COL32(0, 0, 0, 0));
    c[ImGuiCol_TableRowBgAlt] = vec(IM_COL32(255, 255, 255, 7));
    c[ImGuiCol_TextSelectedBg] = vec(withAlpha(col::kAccent, 0.3f));
    c[ImGuiCol_NavCursor] = vec(col::kAccent);
    c[ImGuiCol_ModalWindowDimBg] = vec(IM_COL32(8, 10, 14, 150));
    c[ImGuiCol_PlotLines] = vec(col::kAccent);
    c[ImGuiCol_PlotHistogram] = vec(col::kAccent);

    st.ScaleAllSizes(s);
}

} // namespace

ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 withAlpha(ImU32 c, float alpha) {
    const ImU32 a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF));
    return (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto ch = [&](int shift) {
        const float x = static_cast<float>((a >> shift) & 0xFF), y = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<ImU32>(x + (y - x) * t + 0.5f) << shift;
    };
    return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | ch(IM_COL32_A_SHIFT);
}

ImU32 rgb(unsigned rgb24, float alpha) {
    return IM_COL32((rgb24 >> 16) & 0xFF, (rgb24 >> 8) & 0xFF, rgb24 & 0xFF, static_cast<int>(alpha * 255.0f));
}

Fonts& fonts() { return gFonts; }

float scale() { return gScale; }

void setup(const std::filesystem::path& fontFile, float s) {
    gScale = s;
    ImGuiIO& io = ImGui::GetIO();
    std::error_code ec;
    const bool haveFile = !fontFile.empty() && std::filesystem::exists(fontFile, ec);
    const auto load = [&](float px) -> ImFont* {
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = false;
        if (haveFile) return io.Fonts->AddFontFromFileTTF(fontFile.string().c_str(), px * s, &cfg);
        cfg.SizePixels = std::max(13.0f, px * s * 0.85f);
        return io.Fonts->AddFontDefault(&cfg);
    };
    gFonts.body = load(16.0f); // first font = default font
    gFonts.small = load(13.0f);
    gFonts.heading = load(20.0f);
    gFonts.title = load(46.0f);
    applyStyle(s);
}

} // namespace tp::ui
