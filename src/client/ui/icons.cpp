#include "client/ui/icons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace tp::ui {

namespace {

constexpr float kPi = 3.14159265f;

// Drawing in icon space: (-1, -1) is the top left and (1, 1) the bottom right of the icon box.
struct Pen {
    ImDrawList* dl;
    ImVec2 c;
    float h;
    ImU32 col;
    float t;

    ImVec2 p(float x, float y) const { return ImVec2(c.x + x * h, c.y + y * h); }
    void line(float x0, float y0, float x1, float y1, float w = 1.0f) const { dl->AddLine(p(x0, y0), p(x1, y1), col, t * w); }
    void rect(float x0, float y0, float x1, float y1, float r = 0.0f, float w = 1.0f) const {
        dl->AddRect(p(x0, y0), p(x1, y1), col, r * h, 0, t * w);
    }
    void fillRect(float x0, float y0, float x1, float y1, float r = 0.0f) const {
        dl->AddRectFilled(p(x0, y0), p(x1, y1), col, r * h);
    }
    void circle(float x, float y, float r, float w = 1.0f) const { dl->AddCircle(p(x, y), r * h, col, 0, t * w); }
    void fillCircle(float x, float y, float r) const { dl->AddCircleFilled(p(x, y), r * h, col); }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2) const {
        dl->AddTriangleFilled(p(x0, y0), p(x1, y1), p(x2, y2), col);
    }
    void poly(std::initializer_list<ImVec2> pts, bool closed, float w = 1.0f) const {
        std::array<ImVec2, 16> buf{};
        int n = 0;
        for (const ImVec2 q : pts)
            if (n < 16) buf[static_cast<size_t>(n++)] = p(q.x, q.y);
        dl->AddPolyline(buf.data(), n, col, closed ? ImDrawFlags_Closed : ImDrawFlags_None, t * w);
    }
    void fillPoly(std::initializer_list<ImVec2> pts) const {
        std::array<ImVec2, 16> buf{};
        int n = 0;
        for (const ImVec2 q : pts)
            if (n < 16) buf[static_cast<size_t>(n++)] = p(q.x, q.y);
        dl->AddConvexPolyFilled(buf.data(), n, col);
    }
    void arc(float x, float y, float r, float a0, float a1, float w = 1.0f) const {
        dl->PathArcTo(p(x, y), r * h, a0, a1, 0);
        dl->PathStroke(col, ImDrawFlags_None, t * w);
    }
    // Star with `points` tips, filled as a fan of triangles (it is not convex).
    void star(float x, float y, float r0, float r1, int points, float rotation = -kPi / 2) const {
        const ImVec2 o = p(x, y);
        for (int i = 0; i < points * 2; ++i) {
            const float a0 = rotation + kPi * static_cast<float>(i) / static_cast<float>(points);
            const float a1 = rotation + kPi * static_cast<float>(i + 1) / static_cast<float>(points);
            const float ra = (i % 2 == 0 ? r1 : r0) * h, rb = (i % 2 == 0 ? r0 : r1) * h;
            dl->AddTriangleFilled(o, ImVec2(o.x + std::cos(a0) * ra, o.y + std::sin(a0) * ra),
                                  ImVec2(o.x + std::cos(a1) * rb, o.y + std::sin(a1) * rb), col);
        }
    }
};

} // namespace

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 centre, float size, ImU32 color) {
    const Pen g{dl, centre, size * 0.5f, color, std::max(1.25f, size * 0.085f)};
    switch (icon) {
    case Icon::None: break;
    case Icon::Pause:
        g.fillRect(-0.55f, -0.7f, -0.15f, 0.7f, 0.08f);
        g.fillRect(0.15f, -0.7f, 0.55f, 0.7f, 0.08f);
        break;
    case Icon::Play: g.tri(-0.45f, -0.75f, 0.75f, 0.0f, -0.45f, 0.75f); break;
    case Icon::Fast:
        g.tri(-0.9f, -0.65f, 0.0f, 0.0f, -0.9f, 0.65f);
        g.tri(0.0f, -0.65f, 0.9f, 0.0f, 0.0f, 0.65f);
        break;
    case Icon::Faster:
        g.tri(-0.95f, -0.55f, -0.35f, 0.0f, -0.95f, 0.55f);
        g.tri(-0.35f, -0.55f, 0.25f, 0.0f, -0.35f, 0.55f);
        g.tri(0.25f, -0.55f, 0.85f, 0.0f, 0.25f, 0.55f);
        break;
    case Icon::Landscape:
        g.tri(-0.95f, 0.7f, -0.2f, -0.55f, 0.55f, 0.7f);
        g.tri(0.05f, 0.7f, 0.5f, -0.1f, 0.95f, 0.7f);
        g.circle(0.55f, -0.62f, 0.17f);
        break;
    case Icon::Road:
        g.line(-0.35f, -0.85f, -0.85f, 0.85f);
        g.line(0.35f, -0.85f, 0.85f, 0.85f);
        g.line(0.0f, -0.72f, 0.0f, -0.42f);
        g.line(0.0f, -0.15f, 0.0f, 0.18f);
        g.line(0.0f, 0.46f, 0.0f, 0.85f);
        break;
    case Icon::Bus:
        g.rect(-0.72f, -0.82f, 0.72f, 0.55f, 0.18f);
        g.fillRect(-0.5f, -0.6f, 0.5f, -0.08f, 0.06f);
        g.fillCircle(-0.4f, 0.25f, 0.1f);
        g.fillCircle(0.4f, 0.25f, 0.1f);
        g.fillRect(-0.62f, 0.55f, -0.3f, 0.85f, 0.06f);
        g.fillRect(0.3f, 0.55f, 0.62f, 0.85f, 0.06f);
        break;
    case Icon::Tram:
        g.rect(-0.6f, -0.45f, 0.6f, 0.62f, 0.16f);
        g.fillRect(-0.4f, -0.27f, 0.4f, 0.12f, 0.05f);
        g.line(0.0f, -0.45f, -0.25f, -0.85f);
        g.line(0.0f, -0.45f, 0.25f, -0.85f);
        g.line(-0.35f, -0.85f, 0.35f, -0.85f);
        g.line(-0.85f, 0.88f, 0.85f, 0.88f);
        g.fillCircle(-0.3f, 0.38f, 0.08f);
        g.fillCircle(0.3f, 0.38f, 0.08f);
        break;
    case Icon::Train:
        g.rect(-0.6f, -0.85f, 0.6f, 0.52f, 0.22f);
        g.fillRect(-0.42f, -0.64f, 0.42f, -0.16f, 0.06f);
        g.fillCircle(0.0f, 0.22f, 0.12f);
        g.line(-0.32f, 0.62f, -0.68f, 0.95f);
        g.line(0.32f, 0.62f, 0.68f, 0.95f);
        break;
    case Icon::Ship:
        g.fillPoly({{-0.92f, 0.12f}, {0.92f, 0.12f}, {0.62f, 0.58f}, {-0.62f, 0.58f}});
        g.rect(-0.48f, -0.38f, 0.28f, 0.12f, 0.05f);
        g.fillRect(0.02f, -0.78f, 0.22f, -0.38f);
        dl->AddBezierCubic(g.p(-0.95f, 0.85f), g.p(-0.5f, 0.68f), g.p(0.5f, 1.02f), g.p(0.95f, 0.85f), color, g.t);
        break;
    case Icon::Raise:
        g.line(-0.9f, 0.75f, 0.9f, 0.75f);
        g.line(0.0f, 0.45f, 0.0f, -0.45f, 1.1f);
        g.tri(-0.42f, -0.3f, 0.0f, -0.85f, 0.42f, -0.3f);
        break;
    case Icon::Lower:
        g.line(-0.9f, 0.82f, 0.9f, 0.82f);
        g.line(0.0f, -0.85f, 0.0f, 0.05f, 1.1f);
        g.tri(-0.42f, -0.1f, 0.0f, 0.5f, 0.42f, -0.1f);
        break;
    case Icon::Bulldoze:
        g.fillPoly({{-0.98f, -0.15f}, {-0.78f, -0.05f}, {-0.78f, 0.62f}, {-0.98f, 0.72f}});
        g.line(-0.78f, 0.22f, -0.55f, 0.22f);
        g.rect(-0.55f, -0.05f, 0.62f, 0.42f, 0.06f);
        g.rect(-0.1f, -0.62f, 0.45f, -0.05f, 0.05f);
        g.rect(-0.6f, 0.48f, 0.78f, 0.86f, 0.18f);
        break;
    case Icon::Tree:
        g.tri(-0.62f, 0.38f, 0.0f, -0.9f, 0.62f, 0.38f);
        g.tri(-0.48f, -0.05f, 0.0f, -0.9f, 0.48f, -0.05f);
        g.fillRect(-0.11f, 0.38f, 0.11f, 0.9f);
        break;
    case Icon::Track:
        g.line(-0.38f, -0.92f, -0.38f, 0.92f);
        g.line(0.38f, -0.92f, 0.38f, 0.92f);
        for (const float y : {-0.62f, -0.2f, 0.22f, 0.64f}) g.line(-0.68f, y, 0.68f, y, 1.15f);
        break;
    case Icon::Signal:
        g.line(0.0f, 0.1f, 0.0f, 0.95f);
        g.line(-0.35f, 0.95f, 0.35f, 0.95f);
        g.rect(-0.32f, -0.92f, 0.32f, 0.12f, 0.3f);
        g.fillCircle(0.0f, -0.58f, 0.15f);
        g.circle(0.0f, -0.18f, 0.13f);
        break;
    case Icon::Station:
        g.poly({{-0.95f, -0.28f}, {0.0f, -0.85f}, {0.95f, -0.28f}}, false);
        g.line(-0.95f, -0.28f, 0.95f, -0.28f);
        g.line(-0.6f, -0.28f, -0.6f, 0.52f);
        g.line(0.6f, -0.28f, 0.6f, 0.52f);
        g.fillRect(-0.95f, 0.52f, 0.95f, 0.8f, 0.05f);
        break;
    case Icon::BusStop:
        g.line(0.0f, -0.1f, 0.0f, 0.95f, 1.1f);
        g.circle(0.0f, -0.5f, 0.42f);
        g.fillRect(-0.2f, -0.68f, 0.2f, -0.36f, 0.05f);
        g.line(-0.35f, 0.95f, 0.35f, 0.95f);
        break;
    case Icon::Depot:
        g.poly({{-0.82f, 0.88f}, {-0.82f, -0.2f}, {0.0f, -0.85f}, {0.82f, -0.2f}, {0.82f, 0.88f}}, false);
        g.rect(-0.45f, 0.08f, 0.45f, 0.88f, 0.03f);
        g.line(-0.45f, 0.35f, 0.45f, 0.35f, 0.8f);
        g.line(-0.45f, 0.6f, 0.45f, 0.6f, 0.8f);
        break;
    case Icon::Anchor:
        g.circle(0.0f, -0.7f, 0.16f);
        g.line(0.0f, -0.54f, 0.0f, 0.82f);
        g.line(-0.38f, -0.3f, 0.38f, -0.3f);
        g.arc(0.0f, 0.12f, 0.68f, kPi * 0.12f, kPi * 0.88f);
        g.tri(-0.82f, 0.18f, -0.52f, 0.12f, -0.72f, 0.42f);
        g.tri(0.82f, 0.18f, 0.52f, 0.12f, 0.72f, 0.42f);
        break;
    case Icon::Shipyard:
        g.poly({{-0.82f, 0.45f}, {-0.82f, -0.35f}, {0.0f, -0.9f}, {0.82f, -0.35f}, {0.82f, 0.45f}}, false);
        g.rect(-0.38f, -0.1f, 0.38f, 0.45f, 0.03f);
        dl->AddBezierCubic(g.p(-0.95f, 0.78f), g.p(-0.5f, 0.6f), g.p(0.5f, 0.96f), g.p(0.95f, 0.78f), color, g.t);
        break;
    case Icon::Coins: {
        g.circle(0.0f, 0.0f, 0.82f);
        ImFont* f = ImGui::GetFont();
        const float fs = size * 0.95f;
        const ImVec2 ts = f->CalcTextSizeA(fs, 1000.0f, 0.0f, "$");
        dl->AddText(f, fs, ImVec2(centre.x - ts.x * 0.5f, centre.y - ts.y * 0.5f), color, "$");
        break;
    }
    case Icon::Town:
        g.rect(-0.92f, -0.1f, -0.36f, 0.88f, 0.04f);
        g.rect(-0.3f, -0.85f, 0.26f, 0.88f, 0.04f);
        g.rect(0.32f, -0.38f, 0.9f, 0.88f, 0.04f);
        for (const float y : {-0.55f, -0.2f, 0.15f}) g.fillRect(-0.12f, y, 0.08f, y + 0.14f);
        for (const float y : {0.15f, 0.5f}) g.fillRect(-0.72f, y, -0.54f, y + 0.14f);
        for (const float y : {-0.1f, 0.25f}) g.fillRect(0.52f, y, 0.7f, y + 0.14f);
        break;
    case Icon::News:
        g.rect(-0.85f, -0.72f, 0.85f, 0.72f, 0.1f);
        g.fillRect(-0.6f, -0.48f, 0.6f, -0.2f, 0.03f);
        for (const float y : {0.05f, 0.27f, 0.48f}) g.line(-0.6f, y, -0.08f, y, 0.8f);
        g.rect(0.1f, 0.0f, 0.6f, 0.5f, 0.03f, 0.8f);
        break;
    case Icon::Map:
        g.poly({{-0.9f, -0.62f}, {-0.3f, -0.85f}, {0.3f, -0.62f}, {0.9f, -0.85f}, {0.9f, 0.62f}, {0.3f, 0.85f},
                {-0.3f, 0.62f}, {-0.9f, 0.85f}},
               true);
        g.line(-0.3f, -0.85f, -0.3f, 0.62f, 0.8f);
        g.line(0.3f, -0.62f, 0.3f, 0.85f, 0.8f);
        break;
    case Icon::Calendar:
        g.rect(-0.82f, -0.62f, 0.82f, 0.82f, 0.14f);
        g.fillRect(-0.82f, -0.62f, 0.82f, -0.25f, 0.14f);
        g.line(-0.42f, -0.88f, -0.42f, -0.48f, 1.1f);
        g.line(0.42f, -0.88f, 0.42f, -0.48f, 1.1f);
        for (const float y : {0.05f, 0.45f})
            for (const float x : {-0.45f, 0.0f, 0.45f}) g.fillRect(x - 0.1f, y - 0.08f, x + 0.1f, y + 0.08f);
        break;
    case Icon::Clock:
        g.circle(0.0f, 0.0f, 0.82f);
        g.line(0.0f, 0.0f, 0.0f, -0.52f);
        g.line(0.0f, 0.0f, 0.38f, 0.18f);
        break;
    case Icon::Sun:
        g.fillCircle(0.0f, 0.0f, 0.38f);
        for (int i = 0; i < 8; ++i) {
            const float a = kPi * static_cast<float>(i) / 4.0f;
            g.line(std::cos(a) * 0.58f, std::sin(a) * 0.58f, std::cos(a) * 0.88f, std::sin(a) * 0.88f);
        }
        break;
    case Icon::Moon: {
        // Crescent: the outer circle's left side, then back along an offset inner circle.
        const float r0 = 0.78f, r1 = 0.62f;
        const ImVec2 o = g.p(0.0f, 0.0f), i = g.p(0.32f, -0.22f);
        // The arcs meet where the two circles intersect.
        dl->PathArcTo(o, r0 * g.h, kPi * 0.096f, kPi * 1.521f, 20);
        dl->PathArcTo(i, r1 * g.h, kPi * 1.357f, kPi * 0.26f, 20);
        dl->PathFillConcave(color);
        break;
    }
    case Icon::People:
        g.fillCircle(0.0f, -0.42f, 0.3f);
        dl->PathArcTo(g.p(0.0f, 0.85f), 0.68f * g.h, kPi, 2.0f * kPi, 0);
        dl->PathFillConvex(color);
        break;
    case Icon::Home:
        g.poly({{-0.75f, 0.88f}, {-0.75f, -0.1f}, {0.0f, -0.82f}, {0.75f, -0.1f}, {0.75f, 0.88f}}, true);
        g.fillRect(-0.2f, 0.32f, 0.2f, 0.88f, 0.03f);
        break;
    case Icon::Briefcase:
        g.rect(-0.85f, -0.38f, 0.85f, 0.75f, 0.14f);
        g.rect(-0.3f, -0.72f, 0.3f, -0.38f, 0.08f);
        g.line(-0.85f, 0.1f, 0.85f, 0.1f, 0.8f);
        break;
    case Icon::Cart:
        g.poly({{-0.92f, -0.62f}, {-0.62f, -0.62f}, {-0.38f, 0.32f}, {0.62f, 0.32f}, {0.85f, -0.38f}, {-0.52f, -0.38f}},
               false);
        g.fillCircle(-0.25f, 0.68f, 0.14f);
        g.fillCircle(0.5f, 0.68f, 0.14f);
        break;
    case Icon::Walk:
        g.fillCircle(0.12f, -0.72f, 0.18f);
        g.line(0.06f, -0.45f, -0.04f, 0.18f, 1.15f);
        g.line(-0.04f, 0.18f, -0.42f, 0.88f, 1.15f);
        g.line(-0.04f, 0.18f, 0.36f, 0.88f, 1.15f);
        g.line(0.02f, -0.28f, -0.42f, 0.02f);
        g.line(0.02f, -0.28f, 0.42f, -0.06f);
        break;
    case Icon::Car:
        g.rect(-0.9f, -0.02f, 0.9f, 0.45f, 0.14f);
        g.poly({{-0.52f, -0.02f}, {-0.3f, -0.48f}, {0.36f, -0.48f}, {0.58f, -0.02f}}, false);
        g.fillCircle(-0.5f, 0.52f, 0.2f);
        g.fillCircle(0.5f, 0.52f, 0.2f);
        break;
    case Icon::Chart:
        g.line(-0.9f, 0.85f, 0.9f, 0.85f);
        g.fillRect(-0.7f, 0.2f, -0.35f, 0.85f, 0.04f);
        g.fillRect(-0.18f, -0.25f, 0.18f, 0.85f, 0.04f);
        g.fillRect(0.35f, -0.7f, 0.7f, 0.85f, 0.04f);
        break;
    case Icon::Gauge:
        g.arc(0.0f, 0.3f, 0.82f, kPi, 2.0f * kPi);
        g.line(0.0f, 0.3f, 0.45f, -0.3f, 1.1f);
        g.fillCircle(0.0f, 0.3f, 0.14f);
        break;
    case Icon::Star: g.star(0.0f, 0.08f, 0.4f, 0.92f, 5); break;
    case Icon::Close:
        g.line(-0.6f, -0.6f, 0.6f, 0.6f, 1.15f);
        g.line(0.6f, -0.6f, -0.6f, 0.6f, 1.15f);
        break;
    case Icon::Target:
        g.circle(0.0f, 0.0f, 0.55f);
        g.fillCircle(0.0f, 0.0f, 0.16f);
        g.line(0.0f, -0.95f, 0.0f, -0.62f);
        g.line(0.0f, 0.62f, 0.0f, 0.95f);
        g.line(-0.95f, 0.0f, -0.62f, 0.0f);
        g.line(0.62f, 0.0f, 0.95f, 0.0f);
        break;
    case Icon::Eye:
        dl->AddBezierQuadratic(g.p(-0.92f, 0.0f), g.p(0.0f, -0.85f), g.p(0.92f, 0.0f), color, g.t);
        dl->AddBezierQuadratic(g.p(-0.92f, 0.0f), g.p(0.0f, 0.85f), g.p(0.92f, 0.0f), color, g.t);
        g.fillCircle(0.0f, 0.0f, 0.26f);
        break;
    case Icon::Wrench:
        g.line(-0.72f, 0.72f, 0.12f, -0.12f, 2.0f);
        g.arc(0.38f, -0.38f, 0.36f, kPi * 0.05f, kPi * 1.45f, 1.4f);
        break;
    case Icon::Clone:
        g.rect(-0.78f, -0.3f, 0.32f, 0.8f, 0.14f);
        g.rect(-0.32f, -0.8f, 0.78f, 0.3f, 0.14f);
        break;
    case Icon::Link:
        g.rect(-0.92f, -0.32f, 0.12f, 0.32f, 0.32f);
        g.rect(-0.12f, -0.32f, 0.92f, 0.32f, 0.32f);
        break;
    case Icon::Skip:
        g.tri(-0.62f, -0.62f, 0.32f, 0.0f, -0.62f, 0.62f);
        g.fillRect(0.38f, -0.62f, 0.62f, 0.62f, 0.05f);
        break;
    case Icon::Trash:
        g.line(-0.75f, -0.55f, 0.75f, -0.55f);
        g.rect(-0.22f, -0.82f, 0.22f, -0.55f, 0.05f);
        g.poly({{-0.55f, -0.42f}, {-0.42f, 0.86f}, {0.42f, 0.86f}, {0.55f, -0.42f}}, false);
        g.line(-0.15f, -0.2f, -0.12f, 0.62f, 0.8f);
        g.line(0.15f, -0.2f, 0.12f, 0.62f, 0.8f);
        break;
    case Icon::Plus:
        g.line(-0.65f, 0.0f, 0.65f, 0.0f, 1.2f);
        g.line(0.0f, -0.65f, 0.0f, 0.65f, 1.2f);
        break;
    case Icon::Minus: g.line(-0.65f, 0.0f, 0.65f, 0.0f, 1.2f); break;
    case Icon::ArrowUp: g.tri(-0.62f, 0.38f, 0.0f, -0.48f, 0.62f, 0.38f); break;
    case Icon::ArrowDown: g.tri(-0.62f, -0.38f, 0.62f, -0.38f, 0.0f, 0.48f); break;
    case Icon::Check: g.poly({{-0.72f, 0.02f}, {-0.22f, 0.52f}, {0.75f, -0.5f}}, false, 1.3f); break;
    case Icon::Menu:
        for (const float y : {-0.55f, 0.0f, 0.55f}) g.line(-0.75f, y, 0.75f, y, 1.15f);
        break;
    case Icon::Save:
        g.rect(-0.8f, -0.8f, 0.8f, 0.8f, 0.12f);
        g.fillRect(-0.42f, -0.8f, 0.36f, -0.32f, 0.03f);
        g.rect(-0.48f, 0.18f, 0.48f, 0.8f, 0.03f, 0.8f);
        break;
    case Icon::Chat:
        g.rect(-0.88f, -0.75f, 0.88f, 0.38f, 0.25f);
        g.tri(-0.48f, 0.36f, -0.58f, 0.88f, -0.05f, 0.36f);
        break;
    case Icon::Folder:
        g.poly({{-0.88f, -0.62f}, {-0.3f, -0.62f}, {-0.12f, -0.4f}, {0.88f, -0.4f}, {0.88f, 0.72f}, {-0.88f, 0.72f}},
               true);
        g.line(-0.88f, -0.15f, 0.88f, -0.15f, 0.8f);
        break;
    case Icon::Globe:
        g.circle(0.0f, 0.0f, 0.86f);
        g.line(-0.86f, 0.0f, 0.86f, 0.0f, 0.8f);
        dl->AddBezierQuadratic(g.p(0.0f, -0.86f), g.p(0.75f, 0.0f), g.p(0.0f, 0.86f), color, g.t * 0.8f);
        dl->AddBezierQuadratic(g.p(0.0f, -0.86f), g.p(-0.75f, 0.0f), g.p(0.0f, 0.86f), color, g.t * 0.8f);
        break;
    case Icon::Server:
        g.rect(-0.82f, -0.78f, 0.82f, -0.1f, 0.12f);
        g.rect(-0.82f, 0.1f, 0.82f, 0.78f, 0.12f);
        g.fillCircle(0.5f, -0.44f, 0.11f);
        g.fillCircle(0.5f, 0.44f, 0.11f);
        g.line(-0.55f, -0.44f, 0.15f, -0.44f, 0.8f);
        g.line(-0.55f, 0.44f, 0.15f, 0.44f, 0.8f);
        break;
    case Icon::Power:
        g.arc(0.0f, 0.08f, 0.72f, -kPi * 0.5f + 0.62f, kPi * 1.5f - 0.62f, 1.1f);
        g.line(0.0f, -0.92f, 0.0f, -0.1f, 1.15f);
        break;
    case Icon::Spark:
        g.star(0.0f, 0.0f, 0.22f, 0.95f, 4);
        g.star(0.62f, -0.62f, 0.08f, 0.3f, 4);
        break;
    case Icon::Warning:
        g.poly({{0.0f, -0.86f}, {0.92f, 0.78f}, {-0.92f, 0.78f}}, true);
        g.line(0.0f, -0.28f, 0.0f, 0.24f, 1.15f);
        g.fillCircle(0.0f, 0.5f, 0.1f);
        break;
    case Icon::Info:
        g.circle(0.0f, 0.0f, 0.86f);
        g.fillCircle(0.0f, -0.42f, 0.11f);
        g.line(0.0f, -0.12f, 0.0f, 0.5f, 1.15f);
        break;
    }
}

void iconInline(Icon icon, ImU32 color, float size) {
    const float s = size > 0.0f ? size : ImGui::GetFontSize();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float lineH = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(s, lineH));
    drawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(pos.x + s * 0.5f, pos.y + lineH * 0.5f), s * 0.9f, color);
}

} // namespace tp::ui
