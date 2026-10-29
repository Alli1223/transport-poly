#include "client/ui/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

namespace tp::ui {

namespace {

constexpr ImU32 kBlack = IM_COL32(0, 0, 0, 255);
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);

// Applies the current style alpha (disabled widgets) to a colour.
ImU32 styled(ImU32 c) { return ImGui::GetColorU32(c); }

// End of the visible part of a label ("Text##id" -> "Text").
const char* labelEnd(const char* label) {
    const char* p = label;
    while (*p && !(p[0] == '#' && p[1] == '#')) ++p;
    return p;
}

float px(float v) { return v * scale(); }

} // namespace

float animate(ImGuiID id, float target, float speed) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float* v = st->GetFloatRef(id, target);
    *v += (target - *v) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::abs(target - *v) < 0.001f) *v = target;
    return *v;
}

void dropShadow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, float spread, float alpha) {
    constexpr int kLayers = 7;
    const float s = px(spread);
    for (int i = kLayers; i >= 1; --i) {
        const float k = static_cast<float>(i) / kLayers;
        const float grow = s * k;
        const float a = alpha * (1.0f - k) * (1.0f - k) * 0.55f + alpha * 0.04f;
        dl->AddRectFilled(ImVec2(mn.x - grow, mn.y - grow + s * 0.25f), ImVec2(mx.x + grow, mx.y + grow + s * 0.35f),
                          withAlpha(kBlack, a), rounding + grow);
    }
}

void keyCap(const char* key) {
    FontScope f(fonts().small);
    const ImVec2 ts = ImGui::CalcTextSize(key);
    const ImVec2 pad(px(6), px(1.5f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ts.x + pad.x * 2, ts.y + pad.y * 2));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mx(pos.x + ts.x + pad.x * 2, pos.y + ts.y + pad.y * 2);
    dl->AddRectFilled(pos, mx, col::kFrame, px(4));
    dl->AddLine(ImVec2(pos.x + px(2), mx.y - 0.5f), ImVec2(mx.x - px(2), mx.y - 0.5f), col::kBorderStrong, px(1.5f));
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), col::kTextMuted, key);
}

void tooltip(const Tip& tip) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(9)));
    if (ImGui::BeginTooltip()) {
        if (tip.title) {
            ImGui::TextUnformatted(tip.title);
            if (tip.hotkey) {
                ImGui::SameLine(0, px(10));
                keyCap(tip.hotkey);
            }
        }
        if (tip.body) {
            FontScope f(fonts().small);
            ImGui::PushTextWrapPos(px(280));
            ImGui::PushStyleColor(ImGuiCol_Text, vec(col::kTextMuted));
            ImGui::TextUnformatted(tip.body);
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

void itemTooltip(const Tip& tip) {
    if ((tip.title || tip.body) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) tooltip(tip);
}

bool iconButton(const char* id, Icon icon, float size, bool active, ImU32 accent, const Tip& tip) {
    const float sz = px(size);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImGuiID gid = ImGui::GetItemID();
    const float h = animate(gid, hovered ? 1.0f : 0.0f, 18.0f);
    const float a = animate(gid + 1, active ? 1.0f : 0.0f, 14.0f);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = sz * 0.3f;
    ImU32 bg = mix(withAlpha(col::kFrameHover, 0.0f), col::kFrameHover, h);
    if (a > 0.0f) bg = mix(bg, withAlpha(accent, 0.2f), a);
    if (held) bg = mix(bg, withAlpha(accent, 0.36f), 0.7f);
    dl->AddRectFilled(mn, mx, styled(bg), r);
    if (a > 0.01f) {
        dl->AddRect(mn, mx, styled(withAlpha(accent, 0.75f * a)), r, 0, px(1.5f));
        // Indicator dot under the icon.
        dl->AddCircleFilled(ImVec2((mn.x + mx.x) * 0.5f, mx.y - px(4)), px(2.2f) * a, styled(accent));
    }
    const ImU32 ic = mix(mix(col::kTextMuted, col::kText, h), accent, a);
    const float lift = held ? px(1) : 0.0f;
    drawIcon(dl, icon, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f - (a > 0.01f ? px(1.5f) : 0.0f) + lift),
             sz * 0.48f, styled(ic));
    itemTooltip(tip);
    return pressed;
}

bool miniButton(const char* id, Icon icon, ImU32 color, const char* tip) {
    const float sz = ImGui::GetFrameHeight() * 0.85f;
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered();
    const float h = animate(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 18.0f);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (h > 0.0f) dl->AddRectFilled(mn, mx, styled(withAlpha(col::kFrameHover, h)), px(6));
    drawIcon(dl, icon, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), sz * 0.5f,
             styled(mix(color, col::kText, h * 0.6f)));
    if (tip) itemTooltip(Tip{tip});
    return pressed;
}

bool toolTile(const char* id, Icon icon, const char* label, bool active, ImU32 accent, const Tip& tip,
              bool removeBadge) {
    const ImVec2 size(px(78), px(66));
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImGuiID gid = ImGui::GetItemID();
    const float h = animate(gid, hovered ? 1.0f : 0.0f, 18.0f);
    const float a = animate(gid + 1, active ? 1.0f : 0.0f, 14.0f);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = px(10);
    ImU32 bg = mix(withAlpha(col::kCard, 0.55f), col::kCardHover, h);
    if (a > 0.0f) bg = mix(bg, withAlpha(accent, 0.22f), a);
    if (held) bg = mix(bg, withAlpha(accent, 0.35f), 0.6f);
    dl->AddRectFilled(mn, mx, styled(bg), r);
    dl->AddRect(mn, mx, styled(mix(col::kBorder, withAlpha(accent, 0.85f), a)), r, 0, px(a > 0.01f ? 1.5f : 1.0f));
    const ImVec2 ic((mn.x + mx.x) * 0.5f, mn.y + px(25));
    const ImU32 iconCol = mix(mix(col::kTextMuted, col::kText, h), accent, a);
    drawIcon(dl, icon, ic, px(26), styled(iconCol));
    if (removeBadge) {
        const ImVec2 b(ic.x + px(13), ic.y + px(10));
        dl->AddCircleFilled(b, px(7), styled(col::kRed));
        dl->AddLine(ImVec2(b.x - px(3.5f), b.y), ImVec2(b.x + px(3.5f), b.y), styled(kWhite), px(1.8f));
    }
    {
        FontScope f(fonts().small);
        const char* end = labelEnd(label);
        const ImVec2 ts = ImGui::CalcTextSize(label, end);
        dl->AddText(ImVec2((mn.x + mx.x - ts.x) * 0.5f, mx.y - ts.y - px(8)),
                    styled(mix(col::kTextMuted, col::kText, std::max(h, a))), label, end);
    }
    itemTooltip(tip);
    return pressed;
}

bool button(const char* label, Icon icon, Btn kind, ImVec2 size) {
    const char* end = labelEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float iconSz = ImGui::GetFontSize();
    const bool hasText = end != label;
    const float gap = (icon != Icon::None && hasText) ? px(7) : 0.0f;
    const float content = (icon != Icon::None ? iconSz : 0.0f) + gap + ts.x;
    float w = content + px(hasText ? 26.0f : 16.0f);
    float hgt = ImGui::GetFrameHeight() + px(4);
    if (size.x < 0.0f) w = std::max(w, ImGui::GetContentRegionAvail().x + size.x + 1.0f);
    else if (size.x > 0.0f) w = px(size.x);
    if (size.y > 0.0f) hgt = px(size.y);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, hgt));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const float h = animate(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 18.0f);
    ImU32 bg = 0, fg = col::kText, border = 0;
    switch (kind) {
    case Btn::Primary:
        bg = mix(col::kAccent, mix(col::kAccent, kWhite, 0.18f), h);
        fg = col::kTextOnAccent;
        break;
    case Btn::Secondary:
        bg = mix(col::kFrame, col::kFrameHover, h);
        border = col::kBorder;
        break;
    case Btn::Danger:
        bg = withAlpha(col::kRed, 0.14f + 0.12f * h);
        fg = col::kRed;
        break;
    case Btn::Ghost:
        bg = withAlpha(col::kFrameHover, h);
        fg = mix(col::kTextMuted, col::kText, h);
        break;
    }
    if (held) bg = mix(bg, kBlack, 0.15f);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = std::min(px(9), hgt * 0.5f);
    dl->AddRectFilled(mn, mx, styled(bg), r);
    if (border) dl->AddRect(mn, mx, styled(border), r);
    if (kind == Btn::Primary)
        dl->AddRectFilledMultiColor(ImVec2(mn.x + r, mn.y + 1), ImVec2(mx.x - r, mn.y + hgt * 0.45f),
                                    styled(withAlpha(kWhite, 0.10f)), styled(withAlpha(kWhite, 0.10f)),
                                    styled(withAlpha(kWhite, 0.0f)), styled(withAlpha(kWhite, 0.0f)));
    float x = mn.x + (w - content) * 0.5f;
    const float cy = (mn.y + mx.y) * 0.5f + (held ? px(1) : 0.0f);
    if (icon != Icon::None) {
        drawIcon(dl, icon, ImVec2(x + iconSz * 0.5f, cy), iconSz * 0.92f, styled(fg));
        x += iconSz + gap;
    }
    if (hasText) dl->AddText(ImVec2(x, cy - ts.y * 0.5f), styled(fg), label, end);
    return pressed;
}

int segmented(const char* id, std::span<const Segment> items, int current, float height) {
    ImGui::PushID(id);
    const float hgt = height > 0.0f ? px(height) : ImGui::GetFrameHeight();
    const float padX = px(10);
    const float iconSz = ImGui::GetFontSize() * 0.85f;
    std::vector<float> widths;
    float total = 0.0f;
    for (const Segment& s : items) {
        float w = padX * 2;
        if (s.icon != Icon::None) w += iconSz;
        if (s.label) w += ImGui::CalcTextSize(s.label).x + (s.icon != Icon::None ? px(5) : 0.0f);
        widths.push_back(w);
        total += w;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + total, origin.y + hgt), styled(col::kFrame), hgt * 0.5f);
    // Sliding highlight behind the current item.
    float curX = 0.0f, curW = 0.0f, acc = 0.0f;
    for (usize i = 0; i < items.size(); ++i) {
        if (static_cast<int>(i) == current) {
            curX = acc;
            curW = widths[i];
        }
        acc += widths[i];
    }
    const ImGuiID gid = ImGui::GetID("##slide");
    const float sx = animate(gid, curX, 16.0f), sw = animate(gid + 1, curW, 16.0f);
    if (current >= 0 && sw > 1.0f)
        dl->AddRectFilled(ImVec2(origin.x + sx + px(2), origin.y + px(2)),
                          ImVec2(origin.x + sx + sw - px(2), origin.y + hgt - px(2)), styled(col::kAccent),
                          hgt * 0.5f - px(2));
    int clicked = -1;
    float x = origin.x;
    for (usize i = 0; i < items.size(); ++i) {
        const Segment& s = items[i];
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##seg", ImVec2(widths[i], hgt))) clicked = static_cast<int>(i);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = static_cast<int>(i) == current;
        const ImU32 fg = on ? col::kTextOnAccent : (hovered ? col::kText : col::kTextMuted);
        float cx = x + padX;
        const float cy = origin.y + hgt * 0.5f;
        if (s.icon != Icon::None) {
            drawIcon(dl, s.icon, ImVec2(cx + iconSz * 0.5f, cy), iconSz, styled(fg));
            cx += iconSz + px(5);
        }
        if (s.label) {
            const ImVec2 ts = ImGui::CalcTextSize(s.label);
            dl->AddText(ImVec2(cx, cy - ts.y * 0.5f), styled(fg), s.label);
        }
        if (s.tip && hovered) tooltip(Tip{s.tip});
        x += widths[i];
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(total, hgt));
    ImGui::PopID();
    return clicked;
}

bool beginPanel(const char* id, const char* title, Icon icon, bool* open, const PanelSpec& spec) {
    const float w = px(spec.width);
    ImGui::SetNextWindowPos(spec.pos, spec.cond, spec.pivot);
    ImGuiWindowFlags flags = spec.flags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    if (spec.height > 0.0f) {
        ImGui::SetNextWindowSize(ImVec2(w, px(spec.height)), spec.cond);
        ImGui::SetNextWindowSizeConstraints(ImVec2(px(260), px(160)), ImVec2(FLT_MAX, FLT_MAX));
    } else {
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, FLT_MAX));
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    const bool visible = ImGui::Begin(id, nullptr, flags);
    ImGui::PopStyleVar();
    if (!visible) return false;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    // Accent strip along the top edge.
    dl->AddRectFilled(ImVec2(wp.x + px(22), wp.y), ImVec2(wp.x + ws.x - px(22), wp.y + px(3)), spec.accent, px(2),
                      ImDrawFlags_RoundCornersBottom);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float bubble = px(36);
    dl->AddRectFilled(p, ImVec2(p.x + bubble, p.y + bubble), withAlpha(spec.accent, 0.15f), px(10));
    drawIcon(dl, icon, ImVec2(p.x + bubble * 0.5f, p.y + bubble * 0.5f), bubble * 0.5f, spec.accent);
    const float tx = p.x + bubble + px(11);
    const float clipRight = p.x + avail - (open ? px(30) : 0.0f);
    ImFont* hf = fonts().heading ? fonts().heading : ImGui::GetFont();
    ImFont* sf = fonts().small ? fonts().small : ImGui::GetFont();
    const float hs = hf->FontSize, ss = sf->FontSize;
    const float titleY = spec.subtitle ? p.y + (bubble - hs - ss - px(1)) * 0.5f : p.y + (bubble - hs) * 0.5f;
    dl->PushClipRect(ImVec2(tx, p.y - px(2)), ImVec2(clipRight, p.y + bubble + px(2)), true);
    dl->AddText(hf, hs, ImVec2(tx, titleY), col::kText, title);
    if (spec.subtitle) dl->AddText(sf, ss, ImVec2(tx, titleY + hs + px(1)), col::kTextMuted, spec.subtitle);
    dl->PopClipRect();
    if (open) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + avail - ImGui::GetFrameHeight() * 0.85f, p.y + px(4)));
        if (miniButton("##close", Icon::Close, col::kTextMuted, "Close")) *open = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + bubble + px(10)));
    dl->AddLine(ImVec2(p.x, p.y + bubble + px(5)), ImVec2(p.x + avail, p.y + bubble + px(5)), col::kBorder);
    ImGui::Dummy(ImVec2(0.0f, px(2)));
    return true;
}

void endPanel() { ImGui::End(); }

void beginBar(const char* id, ImVec2 pos, ImVec2 pivot, ImVec2 padding, float rounding) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(padding.x), px(padding.y)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(rounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, vec(col::kBar));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(col::kBorder));
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dropShadow(ImGui::GetBackgroundDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y), px(rounding));
}

void endBar() { ImGui::End(); }

float columnWidth(int columns) {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
}

void sectionHeader(const char* text) {
    ImGui::Dummy(ImVec2(0.0f, px(4)));
    std::string up(text);
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    FontScope f(fonts().small);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 ts = ImGui::CalcTextSize(up.c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(pos, col::kTextMuted, up.c_str());
    if (pos.x + ts.x + px(10) < pos.x + avail)
        dl->AddLine(ImVec2(pos.x + ts.x + px(10), pos.y + ts.y * 0.55f), ImVec2(pos.x + avail, pos.y + ts.y * 0.55f),
                    col::kBorder);
    ImGui::Dummy(ImVec2(0.0f, ts.y + px(1)));
}

void statRow(const char* label, const char* value, ImU32 valueColor) {
    const float x0 = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, vec(col::kTextMuted));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    const float lw = ImGui::CalcTextSize(label).x;
    const float vw = ImGui::CalcTextSize(value).x;
    ImGui::SameLine(x0 + std::max(avail - vw, lw + px(12)));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(valueColor));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

void moneyRow(const char* label, Money value) {
    statRow(label, formatMoney(value).c_str(), value < 0 ? col::kRed : (value > 0 ? col::kGreen : col::kText));
}

void meter(float fraction, ImU32 color, float height) {
    const float hgt = height > 0.0f ? px(height) : px(7);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + hgt), styled(col::kFrame), hgt * 0.5f);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0.0f) {
        const float fw = std::max(hgt, avail * f);
        dl->AddRectFilled(pos, ImVec2(pos.x + fw, pos.y + hgt), styled(color), hgt * 0.5f);
        dl->AddRectFilled(ImVec2(pos.x + hgt * 0.5f, pos.y + 1), ImVec2(pos.x + fw - hgt * 0.5f, pos.y + hgt * 0.45f),
                          styled(withAlpha(kWhite, 0.16f)));
    }
    ImGui::Dummy(ImVec2(avail, hgt));
}

void meterRow(const char* label, const char* value, float fraction, ImU32 color) {
    statRow(label, value, color);
    meter(fraction, color);
}

void badge(const char* text, ImU32 color) {
    FontScope f(fonts().small);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad(px(8), px(2.5f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), styled(withAlpha(color, 0.17f)), size.y * 0.5f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), styled(color), text);
}

void statCard(Icon icon, const char* value, const char* label, ImU32 color, float width) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float hgt = px(60);
    ImGui::Dummy(ImVec2(width, hgt));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mx(pos.x + width, pos.y + hgt);
    dl->AddRectFilled(pos, mx, col::kCard, px(10));
    dl->AddRectFilled(ImVec2(pos.x, pos.y + px(12)), ImVec2(pos.x + px(3), mx.y - px(12)), color, px(2));
    ImFont* sf = fonts().small ? fonts().small : ImGui::GetFont();
    ImFont* hf = fonts().heading ? fonts().heading : ImGui::GetFont();
    dl->PushClipRect(pos, mx, true);
    drawIcon(dl, icon, ImVec2(pos.x + px(19), pos.y + px(9) + sf->FontSize * 0.5f), px(14), color);
    dl->AddText(sf, sf->FontSize, ImVec2(pos.x + px(31), pos.y + px(9)), col::kTextMuted, label);
    dl->AddText(hf, hf->FontSize, ImVec2(pos.x + px(12), pos.y + px(27)), col::kText, value);
    dl->PopClipRect();
}

void stackedBar(std::span<const float> fractions, std::span<const ImU32> colors, float height) {
    const float hgt = height > 0.0f ? px(height) : px(10);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + hgt), col::kFrame, hgt * 0.5f);
    float total = 0.0f;
    for (const float f : fractions) total += std::max(0.0f, f);
    if (total > 0.0f) {
        // Index of the first and last visible part, which get the rounded ends.
        int first = -1, last = -1;
        for (usize i = 0; i < fractions.size(); ++i)
            if (fractions[i] > 0.0f) {
                if (first < 0) first = static_cast<int>(i);
                last = static_cast<int>(i);
            }
        float x = pos.x;
        for (usize i = 0; i < fractions.size() && i < colors.size(); ++i) {
            if (fractions[i] <= 0.0f) continue;
            const float w = avail * fractions[i] / total;
            ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
            if (static_cast<int>(i) == first) corners |= ImDrawFlags_RoundCornersLeft;
            if (static_cast<int>(i) == last) corners |= ImDrawFlags_RoundCornersRight;
            dl->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + w, pos.y + hgt), colors[i], hgt * 0.5f, corners);
            x += w;
        }
    }
    ImGui::Dummy(ImVec2(avail, hgt));
}

void legendItem(ImU32 color, const char* label) {
    FontScope f(fonts().small);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float lh = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + px(4), pos.y + lh * 0.5f), px(4), color);
    ImGui::Dummy(ImVec2(px(11), lh));
    ImGui::SameLine(0, px(3));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(col::kTextMuted));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

void sparkline(std::span<const float> values, ImVec2 size, ImU32 color) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 sz(size.x > 0 ? size.x : ImGui::GetContentRegionAvail().x, px(size.y));
    ImGui::Dummy(sz);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), col::kCard, px(8));
    if (values.size() < 2) return;
    float lo = values[0], hi = values[0];
    for (const float v : values) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    lo = std::min(lo, 0.0f);
    hi = std::max(hi, 0.0f);
    if (hi - lo < 1e-6f) hi = lo + 1.0f;
    const float padY = px(6), padX = px(6);
    const auto pt = [&](usize i) {
        const float x = pos.x + padX + (sz.x - padX * 2) * static_cast<float>(i) / static_cast<float>(values.size() - 1);
        const float y = pos.y + padY + (sz.y - padY * 2) * (1.0f - (values[i] - lo) / (hi - lo));
        return ImVec2(x, y);
    };
    const float zeroY = pos.y + padY + (sz.y - padY * 2) * (1.0f - (0.0f - lo) / (hi - lo));
    dl->AddLine(ImVec2(pos.x + padX, zeroY), ImVec2(pos.x + sz.x - padX, zeroY), col::kBorderStrong);
    for (usize i = 0; i + 1 < values.size(); ++i) {
        const ImVec2 a = pt(i), b = pt(i + 1);
        dl->AddQuadFilled(a, b, ImVec2(b.x, zeroY), ImVec2(a.x, zeroY), withAlpha(color, 0.18f));
    }
    std::vector<ImVec2> line;
    for (usize i = 0; i < values.size(); ++i) line.push_back(pt(i));
    dl->AddPolyline(line.data(), static_cast<int>(line.size()), color, ImDrawFlags_None, px(2));
    dl->AddCircleFilled(line.back(), px(3.5f), color);
}

void spinner(float radius, ImU32 color) {
    const float r = px(radius);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(r * 2, r * 2));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = static_cast<float>(ImGui::GetTime());
    const ImVec2 c(pos.x + r, pos.y + r);
    dl->AddCircle(c, r - px(2), withAlpha(color, 0.15f), 0, px(3));
    const float a0 = t * 5.0f, a1 = a0 + 1.4f + std::sin(t * 2.5f) * 0.8f;
    dl->PathArcTo(c, r - px(2), a0, a1, 0);
    dl->PathStroke(color, ImDrawFlags_None, px(3));
}

void emptyState(Icon icon, const char* text) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(avail, px(52)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawIcon(dl, icon, ImVec2(pos.x + avail * 0.5f, pos.y + px(16)), px(22), col::kTextDim);
    FontScope f(fonts().small);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(pos.x + (avail - ts.x) * 0.5f, pos.y + px(34)), col::kTextDim, text);
}

void iconText(Icon icon, ImU32 iconColor, const char* text, ImU32 textColor) {
    iconInline(icon, iconColor);
    ImGui::SameLine(0, px(6));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(textColor));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void mutedText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, vec(col::kTextMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void fieldLabel(const char* text) {
    FontScope f(fonts().small);
    ImGui::PushStyleColor(ImGuiCol_Text, vec(col::kTextMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void worldLabel(ImDrawList* dl, ImVec2 centre, const char* title, const char* detail, ImU32 dot, float k) {
    ImFont* tf = fonts().body ? fonts().body : ImGui::GetFont();
    ImFont* df = fonts().small ? fonts().small : ImGui::GetFont();
    const float ts = tf->FontSize * k, ds = df->FontSize * k;
    const ImVec2 tsz = tf->CalcTextSizeA(ts, FLT_MAX, 0.0f, title);
    const ImVec2 dsz = detail ? df->CalcTextSizeA(ds, FLT_MAX, 0.0f, detail) : ImVec2(0, 0);
    const float padX = px(10) * k, padY = px(4) * k, dotD = px(8) * k, gap = px(7) * k;
    const float w = padX + dotD + gap + tsz.x + (detail ? gap + dsz.x : 0.0f) + padX;
    const float h = std::max(tsz.y, dsz.y) + padY * 2;
    const ImVec2 mn(centre.x - w * 0.5f, centre.y - h * 0.5f), mx(centre.x + w * 0.5f, centre.y + h * 0.5f);
    dropShadow(dl, mn, mx, h * 0.5f, 8.0f * k, 0.28f);
    dl->AddRectFilled(mn, mx, withAlpha(col::kBar, 0.92f), h * 0.5f);
    dl->AddRect(mn, mx, col::kBorderStrong, h * 0.5f);
    dl->AddCircleFilled(ImVec2(mn.x + padX + dotD * 0.5f, centre.y), dotD * 0.5f, dot);
    const float x = mn.x + padX + dotD + gap;
    dl->AddText(tf, ts, ImVec2(x, centre.y - tsz.y * 0.5f), col::kText, title);
    if (detail) dl->AddText(df, ds, ImVec2(x + tsz.x + gap, centre.y - dsz.y * 0.5f), col::kTextMuted, detail);
}

void iconWorldLabel(ImDrawList* dl, ImVec2 centre, Icon icon, ImU32 badge, const char* title, const char* detail, float k) {
    ImFont* tf = fonts().heading ? fonts().heading : ImGui::GetFont();
    ImFont* df = fonts().small ? fonts().small : ImGui::GetFont();
    const float ts = tf->FontSize * k, ds = df->FontSize * k;
    const ImVec2 tsz = tf->CalcTextSizeA(ts, FLT_MAX, 0.0f, title);
    const ImVec2 dsz = detail ? df->CalcTextSizeA(ds, FLT_MAX, 0.0f, detail) : ImVec2(0, 0);
    const float padX = px(7) * k, padY = px(5) * k, gap = px(8) * k, lineGap = px(1) * k;
    const float textH = tsz.y + (detail ? lineGap + dsz.y : 0.0f);
    const float badgeD = std::max(textH, tsz.y * 1.3f);
    const float h = badgeD + padY * 2;
    const float w = padX + badgeD + gap + std::max(tsz.x, dsz.x) + padX + px(6) * k;
    const ImVec2 mn(centre.x - w * 0.5f, centre.y - h * 0.5f), mx(centre.x + w * 0.5f, centre.y + h * 0.5f);
    dropShadow(dl, mn, mx, h * 0.5f, 10.0f * k, 0.32f);
    dl->AddRectFilled(mn, mx, withAlpha(col::kBar, 0.94f), h * 0.5f);
    dl->AddRect(mn, mx, withAlpha(badge, 0.6f), h * 0.5f, 0, 1.5f * k);
    const ImVec2 bc(mn.x + padX + badgeD * 0.5f, centre.y);
    dl->AddCircleFilled(bc, badgeD * 0.5f, badge);
    drawIcon(dl, icon, bc, badgeD * 0.68f, col::kBar);
    const float x = mn.x + padX + badgeD + gap, top = centre.y - textH * 0.5f;
    dl->AddText(tf, ts, ImVec2(x, top), col::kText, title);
    if (detail) dl->AddText(df, ds, ImVec2(x, top + tsz.y + lineGap), col::kTextMuted, detail);
}

} // namespace tp::ui
