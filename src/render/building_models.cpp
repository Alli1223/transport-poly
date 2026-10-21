// Procedural low poly town buildings. Every model is centred on its footprint with the entrance
// facing +Z; walls use tintable colours so instances can vary their facade colour.

#include "render/models.hpp"
#include "sim/buildings.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace tp::gfx {

namespace {

const u32 kWall = tintable(1.0f);
const u32 kWallShade = tintable(0.86f);
constexpr u32 kRoofTile = rgba(166, 78, 62);
constexpr u32 kRoofSlate = rgba(80, 86, 98);
constexpr u32 kRoofThatch = rgba(196, 166, 96);
constexpr u32 kRoofDark = rgba(70, 62, 60);
constexpr u32 kWindow = rgba(52, 68, 90, 254);
constexpr u32 kDoor = rgba(98, 64, 42);
constexpr u32 kConcrete = rgba(182, 180, 172);
constexpr u32 kConcreteDark = rgba(140, 138, 132);
constexpr u32 kGlass = rgba(96, 142, 178, 254);
constexpr u32 kGlassDark = rgba(62, 96, 132, 254);
constexpr u32 kChimney = rgba(132, 92, 76);
constexpr u32 kRed = rgba(176, 58, 46);
constexpr u32 kAwnings[3] = {rgba(198, 62, 52), rgba(58, 142, 82), rgba(60, 102, 172)};
constexpr u32 kRoofs[3] = {kRoofTile, kRoofSlate, kRoofThatch};
constexpr float kInset = 0.006f;

// Walls of a box (no floor).
void walls(MeshBuilder& mb, glm::vec3 mn, glm::vec3 mx, u32 color) { mb.box(mn, mx, color, 0x3F & ~0x08); }

// A row of `count` window quads on the wall from a to b (horizontal), between heights y0 and y1.
void windowRow(MeshBuilder& mb, glm::vec3 a, glm::vec3 b, float y0, float y1, int count, glm::vec3 normal,
               u32 color = kWindow, float fill = 0.5f) {
    const glm::vec3 off = normal * kInset;
    for (int i = 0; i < count; ++i) {
        const float c = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        const float h = fill * 0.5f / static_cast<float>(count);
        const glm::vec3 p0 = glm::mix(a, b, c - h) + off;
        const glm::vec3 p1 = glm::mix(a, b, c + h) + off;
        mb.quadFacing({p0.x, y0, p0.z}, {p1.x, y0, p1.z}, {p1.x, y1, p1.z}, {p0.x, y1, p0.z}, color, normal);
    }
}

// Windows on all four sides of a box for each floor.
void boxWindows(MeshBuilder& mb, glm::vec3 mn, glm::vec3 mx, int floors, float floorH, float y0, int perSideX,
                int perSideZ, u32 color = kWindow, float fill = 0.5f, float height = 0.5f) {
    for (int f = 0; f < floors; ++f) {
        const float a = y0 + floorH * static_cast<float>(f) + floorH * (1.0f - height) * 0.5f;
        const float b = a + floorH * height;
        windowRow(mb, {mn.x, 0, mx.z}, {mx.x, 0, mx.z}, a, b, perSideX, {0, 0, 1}, color, fill);
        windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, a, b, perSideX, {0, 0, -1}, color, fill);
        windowRow(mb, {mx.x, 0, mn.z}, {mx.x, 0, mx.z}, a, b, perSideZ, {1, 0, 0}, color, fill);
        windowRow(mb, {mn.x, 0, mn.z}, {mn.x, 0, mx.z}, a, b, perSideZ, {-1, 0, 0}, color, fill);
    }
}

void door(MeshBuilder& mb, float x, float z, float w, float h, u32 color = kDoor) {
    mb.quadFacing({x - w * 0.5f, 0, z + kInset}, {x + w * 0.5f, 0, z + kInset}, {x + w * 0.5f, h, z + kInset},
                  {x - w * 0.5f, h, z + kInset}, color, {0, 0, 1});
}

// Gable roof with the ridge along Z (rotates the X variant).
void gableRoofZ(MeshBuilder& mb, glm::vec3 mn, glm::vec3 mx, float y, float ridge, u32 roof, u32 gable) {
    const glm::mat4 saved = mb.transform;
    mb.transform = saved * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    // After rotating by +90 degrees about Y, local x maps to world -z and local z to world x.
    mb.gableRoofX({-mx.z, mn.y, mn.x}, {-mn.z, mx.y, mx.x}, y, ridge, roof, gable);
    mb.transform = saved;
}

void chimney(MeshBuilder& mb, float x, float z, float y0, float h) {
    mb.box({x - 0.04f, y0, z - 0.04f}, {x + 0.04f, y0 + h, z + 0.04f}, kChimney);
}

void parapet(MeshBuilder& mb, glm::vec3 mn, glm::vec3 mx, float y, u32 color) {
    const float t = 0.03f, h = 0.05f;
    mb.box({mn.x, y, mn.z}, {mx.x, y + h, mn.z + t}, color);
    mb.box({mn.x, y, mx.z - t}, {mx.x, y + h, mx.z}, color);
    mb.box({mn.x, y, mn.z}, {mn.x + t, y + h, mx.z}, color);
    mb.box({mx.x - t, y, mn.z}, {mx.x, y + h, mx.z}, color);
}

void flatRoof(MeshBuilder& mb, glm::vec3 mn, glm::vec3 mx, float y, u32 color) {
    mb.quadFacing({mn.x, y, mn.z}, {mx.x, y, mn.z}, {mx.x, y, mx.z}, {mn.x, y, mx.z}, color, {0, 1, 0});
}

void sawtoothRoof(MeshBuilder& mb, glm::vec3 mn, glm::vec3 mx, float y, int teeth, float h, u32 roof, u32 glass) {
    const float step = (mx.z - mn.z) / static_cast<float>(teeth);
    for (int i = 0; i < teeth; ++i) {
        const float z0 = mn.z + step * static_cast<float>(i);
        const float z1 = z0 + step;
        // Sloped face rises towards +Z, vertical glazed face at z1.
        mb.quadFacing({mn.x, y, z0}, {mx.x, y, z0}, {mx.x, y + h, z1}, {mn.x, y + h, z1}, roof, {0, 1, -0.6f});
        mb.quadFacing({mn.x, y, z1}, {mx.x, y, z1}, {mx.x, y + h, z1}, {mn.x, y + h, z1}, glass, {0, 0, 1});
        mb.triangle({mn.x, y, z0}, {mn.x, y + h, z1}, {mn.x, y, z1}, kWallShade);
        mb.triangle({mx.x, y, z0}, {mx.x, y, z1}, {mx.x, y + h, z1}, kWallShade);
    }
}

void cottage(MeshBuilder& mb, u8 v) {
    const glm::vec3 mn(-0.25f, 0, -0.2f), mx(0.25f, 0.3f, 0.2f);
    walls(mb, mn, mx, kWall);
    mb.gableRoofX(mn, mx, 0.3f, 0.22f, kRoofs[v % 3], kWall, 0.04f);
    chimney(mb, 0.16f, -0.05f, 0.3f, 0.26f);
    door(mb, -0.06f, mx.z, 0.09f, 0.18f);
    windowRow(mb, {0.0f, 0, mx.z}, {0.25f, 0, mx.z}, 0.12f, 0.22f, 1, {0, 0, 1});
    windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, 0.12f, 0.22f, 2, {0, 0, -1});
    windowRow(mb, {mx.x, 0, mn.z}, {mx.x, 0, mx.z}, 0.12f, 0.22f, 1, {1, 0, 0});
}

void house(MeshBuilder& mb, u8 v) {
    const float h = v == 2 ? 0.56f : 0.36f;
    const glm::vec3 mn(-0.31f, 0, -0.26f), mx(0.31f, h, 0.26f);
    walls(mb, mn, mx, kWall);
    if (v == 1)
        mb.hipRoof({mn.x - 0.03f, 0, mn.z - 0.03f}, {mx.x + 0.03f, 0, mx.z + 0.03f}, h, 0.24f, kRoofSlate);
    else
        mb.gableRoofX(mn, mx, h, 0.26f, v == 2 ? kRoofSlate : kRoofTile, kWall, 0.04f);
    chimney(mb, -0.2f, 0.0f, h + 0.05f, 0.24f);
    door(mb, 0.0f, mx.z, 0.1f, 0.2f);
    const int floors = v == 2 ? 2 : 1;
    for (int f = 0; f < floors; ++f) {
        const float y0 = 0.1f + 0.24f * static_cast<float>(f);
        windowRow(mb, {mn.x, 0, mx.z}, {-0.06f, 0, mx.z}, y0, y0 + 0.1f, 1, {0, 0, 1});
        windowRow(mb, {0.06f, 0, mx.z}, {mx.x, 0, mx.z}, y0, y0 + 0.1f, 1, {0, 0, 1});
        windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, y0, y0 + 0.1f, 2, {0, 0, -1});
        windowRow(mb, {mx.x, 0, mn.z}, {mx.x, 0, mx.z}, y0, y0 + 0.1f, 1, {1, 0, 0});
        windowRow(mb, {mn.x, 0, mn.z}, {mn.x, 0, mx.z}, y0, y0 + 0.1f, 1, {-1, 0, 0});
    }
}

void terrace(MeshBuilder& mb, u8 v) {
    const glm::vec3 mn(-0.46f, 0, -0.23f), mx(0.46f, 0.5f, 0.23f);
    walls(mb, mn, mx, kWall);
    mb.gableRoofX(mn, mx, 0.5f, 0.24f, v == 0 ? kRoofSlate : kRoofTile, kWall, 0.03f);
    for (int i = 0; i < 3; ++i) {
        const float x = -0.3f + 0.3f * static_cast<float>(i);
        chimney(mb, x + 0.12f, 0.0f, 0.62f, 0.18f);
        door(mb, x - 0.05f, mx.z, 0.08f, 0.2f, i == 1 ? kRed : kDoor);
        windowRow(mb, {x + 0.0f, 0, mx.z}, {x + 0.12f, 0, mx.z}, 0.09f, 0.2f, 1, {0, 0, 1}, kWindow, 0.8f);
    }
    windowRow(mb, {mn.x, 0, mx.z}, {mx.x, 0, mx.z}, 0.3f, 0.41f, 6, {0, 0, 1});
    windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, 0.3f, 0.41f, 6, {0, 0, -1});
    windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, 0.09f, 0.2f, 6, {0, 0, -1});
}

void apartments(MeshBuilder& mb, u8 v) {
    const float h = 1.05f;
    const glm::vec3 mn(-0.4f, 0, -0.37f), mx(0.4f, h, 0.37f);
    walls(mb, mn, mx, kWall);
    boxWindows(mb, mn, mx, 3, 0.33f, 0.04f, 4, 3, kWindow, 0.55f, 0.45f);
    if (v == 1) {
        mb.hipRoof({mn.x - 0.02f, 0, mn.z - 0.02f}, {mx.x + 0.02f, 0, mx.z + 0.02f}, h, 0.2f, kRoofTile);
    } else {
        flatRoof(mb, mn, mx, h, kConcreteDark);
        parapet(mb, mn, mx, h, kConcrete);
        mb.box({-0.12f, h, -0.1f}, {0.1f, h + 0.12f, 0.08f}, kConcrete);
    }
    door(mb, 0.0f, mx.z, 0.14f, 0.2f, kGlassDark);
}

void towerBlock(MeshBuilder& mb, u8 v) {
    const float h = v == 0 ? 3.2f : 2.6f;
    const glm::vec3 mn(-0.35f, 0, -0.35f), mx(0.35f, h, 0.35f);
    walls(mb, mn, mx, kWall);
    const int floors = static_cast<int>(h / 0.32f);
    boxWindows(mb, mn, mx, floors, 0.32f, 0.04f, 1, 1, kWindow, 0.9f, 0.4f);
    flatRoof(mb, mn, mx, h, kConcreteDark);
    parapet(mb, mn, mx, h, kConcrete);
    mb.box({-0.15f, h, -0.12f}, {0.12f, h + 0.18f, 0.12f}, kConcrete);
    door(mb, 0.0f, mx.z, 0.18f, 0.22f, kGlassDark);
}

void cornerShop(MeshBuilder& mb, u8 v) {
    const glm::vec3 mn(-0.35f, 0, -0.3f), mx(0.35f, 0.42f, 0.3f);
    walls(mb, mn, mx, kWall);
    flatRoof(mb, mn, mx, 0.42f, kConcreteDark);
    parapet(mb, mn, mx, 0.42f, kWallShade);
    // Shop window, door, sign band and awning.
    mb.quadFacing({-0.3f, 0.05f, mx.z + kInset}, {0.1f, 0.05f, mx.z + kInset}, {0.1f, 0.25f, mx.z + kInset},
                  {-0.3f, 0.25f, mx.z + kInset}, kGlass, {0, 0, 1});
    door(mb, 0.2f, mx.z, 0.1f, 0.24f, kGlassDark);
    mb.quadFacing({mn.x, 0.31f, mx.z + kInset}, {mx.x, 0.31f, mx.z + kInset}, {mx.x, 0.39f, mx.z + kInset},
                  {mn.x, 0.39f, mx.z + kInset}, kAwnings[(v + 1) % 3], {0, 0, 1});
    mb.quadFacing({mn.x, 0.29f, mx.z}, {mx.x, 0.29f, mx.z}, {mx.x, 0.22f, mx.z + 0.12f}, {mn.x, 0.22f, mx.z + 0.12f},
                  kAwnings[v % 3], {0, 1, 0.5f});
    windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, 0.14f, 0.26f, 2, {0, 0, -1});
}

void highStreet(MeshBuilder& mb, u8 v) {
    const glm::vec3 mn(-0.46f, 0, -0.4f), mx(0.46f, 0.78f, 0.4f);
    walls(mb, mn, mx, kWall);
    flatRoof(mb, mn, mx, 0.78f, kConcreteDark);
    parapet(mb, mn, mx, 0.78f, kWallShade);
    for (int i = 0; i < 2; ++i) {
        const float x0 = -0.42f + 0.44f * static_cast<float>(i);
        mb.quadFacing({x0, 0.04f, mx.z + kInset}, {x0 + 0.32f, 0.04f, mx.z + kInset},
                      {x0 + 0.32f, 0.26f, mx.z + kInset}, {x0, 0.26f, mx.z + kInset}, kGlass, {0, 0, 1});
        mb.quadFacing({x0, 0.33f, mx.z}, {x0 + 0.38f, 0.33f, mx.z}, {x0 + 0.38f, 0.26f, mx.z + 0.12f},
                      {x0, 0.26f, mx.z + 0.12f}, kAwnings[(v + i) % 3], {0, 1, 0.5f});
    }
    windowRow(mb, {mn.x, 0, mx.z}, {mx.x, 0, mx.z}, 0.46f, 0.64f, 4, {0, 0, 1});
    windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, 0.46f, 0.64f, 4, {0, 0, -1});
    windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, 0.1f, 0.26f, 3, {0, 0, -1});
    windowRow(mb, {mx.x, 0, mn.z}, {mx.x, 0, mx.z}, 0.46f, 0.64f, 3, {1, 0, 0});
    windowRow(mb, {mn.x, 0, mn.z}, {mn.x, 0, mx.z}, 0.46f, 0.64f, 3, {-1, 0, 0});
}

void departmentStore(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.92f, 0, -0.85f), mx(0.92f, 1.0f, 0.85f);
    walls(mb, mn, mx, kWall);
    flatRoof(mb, mn, mx, 1.0f, kConcreteDark);
    parapet(mb, mn, mx, 1.0f, kConcrete);
    mb.box({-0.5f, 1.0f, -0.4f}, {0.1f, 1.18f, 0.1f}, kConcrete);
    mb.box({0.3f, 1.0f, -0.6f}, {0.6f, 1.12f, -0.2f}, kConcreteDark);
    mb.quadFacing({-0.8f, 0.05f, mx.z + kInset}, {0.8f, 0.05f, mx.z + kInset}, {0.8f, 0.4f, mx.z + kInset},
                  {-0.8f, 0.4f, mx.z + kInset}, kGlass, {0, 0, 1});
    mb.quadFacing({-0.7f, 0.62f, mx.z + kInset}, {0.7f, 0.62f, mx.z + kInset}, {0.7f, 0.86f, mx.z + kInset},
                  {-0.7f, 0.86f, mx.z + kInset}, kRed, {0, 0, 1});
    mb.quadFacing({mn.x, 0.48f, mx.z}, {mx.x, 0.48f, mx.z}, {mx.x, 0.42f, mx.z + 0.16f}, {mn.x, 0.42f, mx.z + 0.16f},
                  kConcreteDark, {0, 1, 0.5f});
    windowRow(mb, {mx.x, 0, mn.z}, {mx.x, 0, mx.z}, 0.55f, 0.8f, 5, {1, 0, 0}, kGlassDark);
    windowRow(mb, {mn.x, 0, mn.z}, {mn.x, 0, mx.z}, 0.55f, 0.8f, 5, {-1, 0, 0}, kGlassDark);
}

void smallOffice(MeshBuilder& mb, u8 v) {
    const float h = 1.4f;
    const glm::vec3 mn(-0.38f, 0, -0.38f), mx(0.38f, h, 0.38f);
    walls(mb, mn, mx, v == 0 ? kConcrete : kWall);
    boxWindows(mb, mn, mx, 4, 0.34f, 0.03f, 1, 1, v == 0 ? kGlass : kGlassDark, 0.92f, 0.6f);
    flatRoof(mb, mn, mx, h, kConcreteDark);
    parapet(mb, mn, mx, h, kConcrete);
    door(mb, 0.0f, mx.z, 0.2f, 0.22f, kGlassDark);
}

void officeTower(MeshBuilder& mb, u8 v) {
    const float h = v == 0 ? 4.2f : 3.4f;
    const glm::vec3 mn(-0.31f, 0, -0.31f), mx(0.31f, h, 0.31f);
    const u32 glass = v == 0 ? kGlass : kGlassDark;
    mb.box(mn, mx, glass, 0x3F & ~0x08);
    // Horizontal floor bands.
    for (float y = 0.3f; y < h - 0.1f; y += 0.3f) {
        const float t = 0.02f;
        mb.box({mn.x - 0.004f, y, mn.z - 0.004f}, {mx.x + 0.004f, y + t, mx.z + 0.004f}, kConcrete, 0x3F & ~0x0C);
    }
    flatRoof(mb, mn, mx, h, kConcreteDark);
    if (v == 0) {
        mb.box({-0.2f, h, -0.2f}, {0.2f, h + 0.3f, 0.2f}, glass);
        mb.prism({0, h + 0.3f, 0}, 0.02f, 0.5f, 4, kConcrete, false);
    } else {
        mb.frustum({0, h, 0}, 0.3f, 0.12f, 0.4f, 4, kConcrete);
    }
}

void workshop(MeshBuilder& mb, u8 v) {
    const glm::vec3 mn(-0.41f, 0, -0.35f), mx(0.41f, 0.38f, 0.35f);
    walls(mb, mn, mx, kWall);
    sawtoothRoof(mb, mn, mx, 0.38f, 3, 0.16f, v == 0 ? kRoofSlate : kRoofDark, kGlassDark);
    mb.quadFacing({-0.2f, 0, mx.z + kInset}, {0.15f, 0, mx.z + kInset}, {0.15f, 0.28f, mx.z + kInset},
                  {-0.2f, 0.28f, mx.z + kInset}, kConcreteDark, {0, 0, 1});
    door(mb, 0.28f, mx.z, 0.08f, 0.18f);
}

void factory(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.85f, 0, -0.8f), mx(0.6f, 0.55f, 0.8f);
    walls(mb, mn, mx, kWall);
    sawtoothRoof(mb, mn, mx, 0.55f, 5, 0.2f, kRoofSlate, kGlassDark);
    const glm::vec3 omn(0.6f, 0, 0.1f), omx(0.88f, 0.45f, 0.8f);
    walls(mb, omn, omx, kConcrete);
    flatRoof(mb, omn, omx, 0.45f, kConcreteDark);
    windowRow(mb, {omn.x, 0, omx.z}, {omx.x, 0, omx.z}, 0.25f, 0.38f, 2, {0, 0, 1});
    mb.prism({0.72f, 0, -0.45f}, 0.09f, 1.7f, 8, kRed, true);
    mb.prism({0.72f, 1.2f, -0.45f}, 0.095f, 0.12f, 8, rgba(235, 235, 230), false);
    mb.quadFacing({-0.5f, 0, mx.z + kInset}, {0.0f, 0, mx.z + kInset}, {0.0f, 0.36f, mx.z + kInset},
                  {-0.5f, 0.36f, mx.z + kInset}, kConcreteDark, {0, 0, 1});
}

void farm(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.38f, 0, -0.2f), mx(0.14f, 0.32f, 0.22f);
    walls(mb, mn, mx, rgba(168, 54, 44));
    mb.gableRoofX(mn, mx, 0.32f, 0.2f, kRoofDark, rgba(168, 54, 44), 0.03f);
    mb.quadFacing({-0.2f, 0, mx.z + kInset}, {-0.04f, 0, mx.z + kInset}, {-0.04f, 0.22f, mx.z + kInset},
                  {-0.2f, 0.22f, mx.z + kInset}, rgba(236, 232, 220), {0, 0, 1});
    mb.prism({0.3f, 0, -0.05f}, 0.11f, 0.62f, 10, kConcrete, false);
    mb.cone({0.3f, 0.62f, -0.05f}, 0.12f, 0.12f, 10, kRoofSlate);
}

void church(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.18f, 0, -0.38f), mx(0.18f, 0.42f, 0.22f);
    walls(mb, mn, mx, rgba(206, 200, 184));
    gableRoofZ(mb, mn, mx, 0.42f, 0.26f, kRoofSlate, rgba(206, 200, 184));
    const glm::vec3 tmn(-0.11f, 0, 0.18f), tmx(0.11f, 0.85f, 0.4f);
    walls(mb, tmn, tmx, rgba(196, 190, 174));
    mb.cone({0, 0.85f, 0.29f}, 0.15f, 0.45f, 4, kRoofSlate, 0.785f);
    door(mb, 0.0f, tmx.z, 0.08f, 0.2f);
    windowRow(mb, {mx.x, 0, mn.z}, {mx.x, 0, 0.15f}, 0.12f, 0.32f, 3, {1, 0, 0});
    windowRow(mb, {mn.x, 0, mn.z}, {mn.x, 0, 0.15f}, 0.12f, 0.32f, 3, {-1, 0, 0});
}

void school(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.42f, 0, -0.38f), mx(0.42f, 0.46f, -0.02f);
    walls(mb, mn, mx, kWall);
    flatRoof(mb, mn, mx, 0.46f, kConcreteDark);
    const glm::vec3 wmn(-0.42f, 0, -0.02f), wmx(-0.1f, 0.4f, 0.38f);
    walls(mb, wmn, wmx, kWall);
    flatRoof(mb, wmn, wmx, 0.4f, kConcreteDark);
    windowRow(mb, {-0.1f, 0, mx.z}, {mx.x, 0, mx.z}, 0.1f, 0.34f, 3, {0, 0, 1});
    windowRow(mb, {mn.x, 0, mn.z}, {mx.x, 0, mn.z}, 0.1f, 0.34f, 5, {0, 0, -1});
    door(mb, -0.26f, wmx.z, 0.12f, 0.22f);
    mb.prism({0.3f, 0, 0.25f}, 0.012f, 0.7f, 4, kConcrete, false);
    mb.quadFacing({0.31f, 0.55f, 0.25f}, {0.45f, 0.55f, 0.25f}, {0.45f, 0.66f, 0.25f}, {0.31f, 0.66f, 0.25f},
                  kAwnings[2], {0, 0, 1});
    mb.quadFacing({0.31f, 0.55f, 0.25f}, {0.45f, 0.55f, 0.25f}, {0.45f, 0.66f, 0.25f}, {0.31f, 0.66f, 0.25f},
                  kAwnings[2], {0, 0, -1});
}

// --- Countryside industries ---------------------------------------------------------------

constexpr u32 kBarnRed = rgba(168, 54, 44);
constexpr u32 kTimber = rgba(150, 108, 70);
constexpr u32 kLog = rgba(118, 84, 54);
constexpr u32 kLogEnd = rgba(196, 160, 112);
constexpr u32 kPlank = rgba(214, 180, 126);
constexpr u32 kRockGrey = rgba(150, 146, 138);
constexpr u32 kRockDark = rgba(116, 112, 106);
constexpr u32 kSteelGrey = rgba(96, 104, 114);
constexpr u32 kBrick = rgba(166, 74, 56);
constexpr u32 kFence = rgba(176, 150, 112);
constexpr u32 kPasture = rgba(112, 172, 80);

// A field of crop rows (along X) laid on the ground.
void field(MeshBuilder& mb, float x0, float z0, float x1, float z1, u32 a, u32 b, int rows) {
    for (int i = 0; i < rows; ++i) {
        const float za = z0 + (z1 - z0) * static_cast<float>(i) / rows, zb = z0 + (z1 - z0) * static_cast<float>(i + 1) / rows;
        mb.quadFacing({x0, 0.012f, za}, {x1, 0.012f, za}, {x1, 0.012f, zb}, {x0, 0.012f, zb}, i % 2 ? a : b, {0, 1, 0});
        if (i % 2 == 0) mb.box({x0 + 0.02f, 0.012f, za + 0.01f}, {x1 - 0.02f, 0.05f, zb - 0.01f}, shade(a, 1.05f), 0x3F & ~0x08);
    }
}

// A post and rail fence around a rectangle.
void fence(MeshBuilder& mb, float x0, float z0, float x1, float z1) {
    const float h = 0.09f;
    for (const auto& [a, b] : {std::pair{glm::vec2(x0, z0), glm::vec2(x1, z0)}, std::pair{glm::vec2(x1, z0), glm::vec2(x1, z1)},
                               std::pair{glm::vec2(x1, z1), glm::vec2(x0, z1)}, std::pair{glm::vec2(x0, z1), glm::vec2(x0, z0)}}) {
        const float len = glm::length(b - a);
        const int posts = std::max(2, static_cast<int>(len / 0.25f) + 1);
        for (int i = 0; i < posts; ++i) {
            const glm::vec2 p = glm::mix(a, b, static_cast<float>(i) / (posts - 1));
            mb.box({p.x - 0.01f, 0, p.y - 0.01f}, {p.x + 0.01f, h, p.y + 0.01f}, kFence);
        }
        const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
        mb.box({lo.x - 0.005f, h * 0.65f, lo.y - 0.005f}, {hi.x + 0.005f, h * 0.65f + 0.012f, hi.y + 0.005f}, kFence);
    }
}

void sheep(MeshBuilder& mb, float x, float z, float yaw) {
    const glm::vec2 d(std::cos(yaw), std::sin(yaw));
    mb.icosphere({x, 0.06f, z}, 0.055f, rgba(240, 238, 230), 0, 0.75f);
    mb.box({x + d.x * 0.05f - 0.018f, 0.05f, z + d.y * 0.05f - 0.018f}, {x + d.x * 0.05f + 0.018f, 0.09f, z + d.y * 0.05f + 0.018f},
           rgba(48, 44, 42));
}

void cow(MeshBuilder& mb, float x, float z, bool alongX) {
    const glm::vec3 half = alongX ? glm::vec3(0.07f, 0, 0.035f) : glm::vec3(0.035f, 0, 0.07f);
    mb.box({x - half.x, 0.04f, z - half.z}, {x + half.x, 0.1f, z + half.z}, rgba(238, 236, 230));
    mb.box({x - half.x * 0.4f, 0.1f, z - half.z * 0.6f}, {x + half.x * 0.3f, 0.102f, z + half.z * 0.2f}, rgba(40, 38, 38));
    const glm::vec3 head = alongX ? glm::vec3(x + half.x + 0.02f, 0.09f, z) : glm::vec3(x, 0.09f, z + half.z + 0.02f);
    mb.box(head - glm::vec3(0.022f, 0.025f, 0.022f), head + glm::vec3(0.022f, 0.02f, 0.022f), rgba(52, 46, 44));
    for (const float s : {-1.0f, 1.0f})
        mb.box({x + (alongX ? s * half.x * 0.7f : -0.02f) - 0.01f, 0, z + (alongX ? -0.02f : s * half.z * 0.7f) - 0.01f},
               {x + (alongX ? s * half.x * 0.7f : 0.02f) + 0.01f, 0.04f, z + (alongX ? 0.02f : s * half.z * 0.7f) + 0.01f},
               rgba(60, 56, 52));
}

// A pile of logs lying along X.
void logPile(MeshBuilder& mb, float x, float z, float len, int layers) {
    for (int layer = 0; layer < layers; ++layer)
        for (int i = 0; i < layers - layer + 1; ++i) {
            const float zc = z + (static_cast<float>(i) - (layers - layer) * 0.5f) * 0.07f;
            const float y = layer * 0.06f;
            const u32 face[6] = {kLogEnd, kLogEnd, kLog, kLog, kLog, kLog};
            mb.box6({x - len * 0.5f, y, zc - 0.03f}, {x + len * 0.5f, y + 0.06f, zc + 0.03f}, face);
        }
}

void plankStack(MeshBuilder& mb, float x, float z) {
    for (int i = 0; i < 4; ++i) {
        mb.box({x - 0.16f, i * 0.035f, z - 0.08f}, {x + 0.16f, i * 0.035f + 0.03f, z + 0.08f}, i % 2 ? kPlank : shade(kPlank, 0.92f));
        mb.box({x - 0.14f, i * 0.035f + 0.03f, z - 0.075f}, {x - 0.12f, i * 0.035f + 0.035f, z + 0.075f}, kTimber);
    }
}

void heap(MeshBuilder& mb, float x, float z, float r, float h, u32 c) { mb.cone({x, 0, z}, r, h, 6, c, 0.3f); }

void silo(MeshBuilder& mb, float x, float z, float r, float h) {
    mb.prism({x, 0, z}, r, h, 10, kConcrete, false);
    mb.cone({x, h, z}, r * 1.05f, r * 0.9f, 10, kRoofSlate);
}

void tank(MeshBuilder& mb, float x, float z, float r, float h, u32 c) {
    mb.prism({x, 0, z}, r, h, 12, c, true);
    mb.prism({x, h, z}, r * 1.02f, 0.02f, 12, shade(c, 0.85f), true);
}

// A pithead: two legs leaning towards the winding wheel at the top.
void headframe(MeshBuilder& mb, float x, float z, float h) {
    for (const float s : {-1.0f, 1.0f}) {
        mb.box({x - 0.02f + s * 0.12f, 0, z - 0.1f}, {x + 0.02f + s * 0.12f, h, z - 0.06f}, kSteelGrey);
        mb.box({x - 0.02f + s * 0.12f, 0, z + 0.06f}, {x + 0.02f + s * 0.12f, h, z + 0.1f}, kSteelGrey);
    }
    for (float y = 0.25f; y < h; y += 0.25f) mb.box({x - 0.14f, y, z - 0.1f}, {x + 0.14f, y + 0.02f, z + 0.1f}, kSteelGrey);
    mb.box({x - 0.16f, h, z - 0.12f}, {x + 0.16f, h + 0.04f, z + 0.12f}, shade(kSteelGrey, 0.8f));
    mb.prism({x, h + 0.04f, z}, 0.1f, 0.03f, 10, rgba(70, 74, 80), true);
}

void tallChimney(MeshBuilder& mb, float x, float z, float h, u32 c) {
    mb.frustum({x, 0, z}, 0.08f, 0.055f, h, 8, c);
    mb.prism({x, h - 0.12f, z}, 0.06f, 0.06f, 8, rgba(232, 230, 222), false);
}

void grainFarm(MeshBuilder& mb, u8) {
    field(mb, -1.45f, -1.45f, 0.2f, 1.45f, rgba(214, 186, 92), rgba(190, 160, 76), 12);
    field(mb, 0.25f, -1.45f, 1.45f, -0.05f, rgba(126, 168, 72), rgba(108, 150, 64), 8);
    const glm::vec3 mn(0.45f, 0, 0.25f), mx(1.15f, 0.36f, 0.85f);
    walls(mb, mn, mx, kBarnRed);
    mb.gableRoofX(mn, mx, 0.36f, 0.22f, kRoofDark, kBarnRed, 0.03f);
    mb.quadFacing({0.7f, 0, mx.z + kInset}, {0.9f, 0, mx.z + kInset}, {0.9f, 0.26f, mx.z + kInset}, {0.7f, 0.26f, mx.z + kInset},
                  rgba(236, 232, 220), {0, 0, 1});
    silo(mb, 1.3f, 0.35f, 0.12f, 0.8f);
    silo(mb, 1.3f, 0.75f, 0.12f, 0.7f);
    const glm::vec3 hmn(0.4f, 0, 1.0f), hmx(0.8f, 0.3f, 1.35f);
    walls(mb, hmn, hmx, kWall);
    mb.gableRoofX(hmn, hmx, 0.3f, 0.16f, kRoofTile, kWall, 0.03f);
    windowRow(mb, {hmn.x, 0, hmx.z}, {hmx.x, 0, hmx.z}, 0.12f, 0.22f, 2, {0, 0, 1});
}

void sheepFarm(MeshBuilder& mb, u8) {
    mb.quadFacing({-1.45f, 0.01f, -1.45f}, {1.45f, 0.01f, -1.45f}, {1.45f, 0.01f, 1.45f}, {-1.45f, 0.01f, 1.45f}, kPasture, {0, 1, 0});
    fence(mb, -1.4f, -1.4f, 0.6f, 1.4f);
    static constexpr float kSheep[][3] = {{-1.1f, -1.0f, 0.4f}, {-0.7f, -0.6f, 2.1f}, {-0.3f, -1.1f, 1.0f}, {-1.0f, 0.1f, 3.0f},
                                          {-0.5f, 0.4f, 0.2f},  {0.1f, -0.3f, 4.0f},  {-0.9f, 0.9f, 5.1f}, {0.2f, 0.8f, 1.7f},
                                          {-0.2f, 1.1f, 2.8f},  {0.3f, -1.0f, 3.6f}};
    for (const auto& s : kSheep) sheep(mb, s[0], s[1], s[2]);
    const glm::vec3 mn(0.8f, 0, -0.9f), mx(1.35f, 0.3f, 0.1f);
    walls(mb, mn, mx, kTimber);
    gableRoofZ(mb, mn, mx, 0.3f, 0.18f, kRoofSlate, kTimber);
    const glm::vec3 hmn(0.75f, 0, 0.5f), hmx(1.3f, 0.3f, 1.25f);
    walls(mb, hmn, hmx, kWall);
    mb.gableRoofX(hmn, hmx, 0.3f, 0.17f, kRoofThatch, kWall, 0.03f);
    windowRow(mb, {hmn.x, 0, hmx.z}, {hmx.x, 0, hmx.z}, 0.12f, 0.22f, 2, {0, 0, 1});
}

void dairyFarm(MeshBuilder& mb, u8) {
    mb.quadFacing({-1.45f, 0.01f, -1.45f}, {1.45f, 0.01f, -1.45f}, {1.45f, 0.01f, 1.45f}, {-1.45f, 0.01f, 1.45f}, kPasture, {0, 1, 0});
    fence(mb, -1.4f, -1.4f, 0.2f, 1.4f);
    static constexpr float kCows[][3] = {{-1.0f, -1.0f, 1}, {-0.5f, -0.7f, 0}, {-0.1f, -1.1f, 1}, {-1.1f, 0.0f, 0},
                                         {-0.6f, 0.3f, 1},  {-0.2f, 0.9f, 0},  {-0.9f, 1.0f, 1}};
    for (const auto& c : kCows) cow(mb, c[0], c[1], c[2] > 0.5f);
    const glm::vec3 mn(0.4f, 0, -1.2f), mx(1.35f, 0.42f, -0.2f);
    walls(mb, mn, mx, kBarnRed);
    gableRoofZ(mb, mn, mx, 0.42f, 0.26f, kRoofDark, kBarnRed);
    silo(mb, 1.25f, 0.1f, 0.13f, 0.85f);
    const glm::vec3 smn(0.4f, 0, 0.4f), smx(1.0f, 0.26f, 1.2f);
    walls(mb, smn, smx, rgba(232, 232, 228));
    flatRoof(mb, smn, smx, 0.26f, kConcreteDark);
    tank(mb, 1.2f, 0.8f, 0.1f, 0.3f, rgba(206, 210, 214));
}

void loggingCamp(MeshBuilder& mb, u8) {
    mb.quadFacing({-0.95f, 0.008f, -0.95f}, {0.95f, 0.008f, -0.95f}, {0.95f, 0.008f, 0.95f}, {-0.95f, 0.008f, 0.95f},
                  rgba(132, 112, 84), {0, 1, 0});
    logPile(mb, -0.45f, -0.5f, 0.7f, 3);
    logPile(mb, -0.45f, 0.15f, 0.7f, 2);
    logPile(mb, 0.4f, -0.55f, 0.55f, 2);
    const glm::vec3 mn(0.1f, 0, 0.3f), mx(0.8f, 0.3f, 0.85f);
    walls(mb, mn, mx, kTimber);
    mb.gableRoofX(mn, mx, 0.3f, 0.18f, kRoofDark, kTimber, 0.04f);
    door(mb, 0.3f, mx.z, 0.1f, 0.18f);
    // A loading crane.
    mb.box({0.75f, 0, -0.1f}, {0.8f, 0.6f, -0.05f}, kSteelGrey);
    mb.box({0.2f, 0.55f, -0.1f}, {0.8f, 0.6f, -0.05f}, kSteelGrey);
}

void quarry(MeshBuilder& mb, u8) {
    // Stepped benches of cut rock rising on the north side.
    for (int i = 0; i < 4; ++i) {
        const float z0 = -1.45f, z1 = -0.25f - i * 0.28f;
        mb.box({-1.45f + i * 0.12f, 0, z0}, {0.5f - i * 0.12f, 0.12f + i * 0.14f, z1}, i % 2 ? kRockGrey : kRockDark);
    }
    heap(mb, -0.9f, 0.6f, 0.32f, 0.3f, kRockGrey);
    heap(mb, -0.35f, 0.9f, 0.26f, 0.24f, shade(kRockGrey, 1.08f));
    const glm::vec3 mn(0.6f, 0, -0.5f), mx(1.35f, 0.5f, 0.2f);
    walls(mb, mn, mx, kConcrete);
    flatRoof(mb, mn, mx, 0.5f, kConcreteDark);
    // Conveyor from the crusher to the stockpile.
    mb.box({0.2f, 0.38f, 0.35f}, {0.95f, 0.42f, 0.45f}, kSteelGrey);
    for (const float x : {0.3f, 0.6f, 0.9f}) mb.box({x - 0.015f, 0, 0.38f}, {x + 0.015f, 0.38f, 0.42f}, kSteelGrey);
    heap(mb, 0.15f, 0.4f, 0.2f, 0.32f, rgba(180, 176, 166));
}

void ironMine(MeshBuilder& mb, u8) {
    headframe(mb, -0.45f, -0.4f, 0.95f);
    const glm::vec3 mn(-0.1f, 0, -0.85f), mx(0.8f, 0.36f, -0.15f);
    walls(mb, mn, mx, kBrick);
    mb.gableRoofX(mn, mx, 0.36f, 0.16f, kRoofSlate, kBrick, 0.03f);
    windowRow(mb, {mn.x, 0, mx.z}, {mx.x, 0, mx.z}, 0.14f, 0.26f, 3, {0, 0, 1});
    heap(mb, -0.4f, 0.5f, 0.32f, 0.3f, rgba(146, 84, 62));
    heap(mb, 0.35f, 0.55f, 0.26f, 0.22f, rgba(128, 72, 56));
    tallChimney(mb, 0.65f, -0.65f, 0.9f, kBrick);
}

void coalMine(MeshBuilder& mb, u8) {
    headframe(mb, 0.45f, -0.4f, 1.05f);
    const glm::vec3 mn(-0.85f, 0, -0.85f), mx(0.1f, 0.38f, -0.15f);
    walls(mb, mn, mx, kBrick);
    mb.gableRoofX(mn, mx, 0.38f, 0.18f, kRoofDark, kBrick, 0.03f);
    windowRow(mb, {mn.x, 0, mx.z}, {mx.x, 0, mx.z}, 0.14f, 0.28f, 3, {0, 0, 1});
    heap(mb, -0.35f, 0.5f, 0.34f, 0.3f, rgba(44, 44, 48));
    heap(mb, 0.4f, 0.55f, 0.28f, 0.24f, rgba(56, 56, 60));
    mb.box({-0.15f, 0.3f, 0.1f}, {0.4f, 0.34f, 0.2f}, kSteelGrey);
}

void cheeseDairy(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.85f, 0, -0.7f), mx(0.35f, 0.4f, 0.6f);
    walls(mb, mn, mx, rgba(240, 238, 230));
    mb.gableRoofX(mn, mx, 0.4f, 0.2f, rgba(70, 110, 170), rgba(240, 238, 230), 0.03f);
    windowRow(mb, {mn.x, 0, mx.z}, {mx.x, 0, mx.z}, 0.16f, 0.3f, 4, {0, 0, 1});
    door(mb, 0.15f, mx.z, 0.12f, 0.22f);
    for (int i = 0; i < 3; ++i) tank(mb, 0.65f, -0.55f + i * 0.4f, 0.14f, 0.5f, rgba(212, 216, 222));
}

void textileMill(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.85f, 0, -0.6f), mx(0.6f, 0.6f, 0.5f);
    walls(mb, mn, mx, kBrick);
    flatRoof(mb, mn, mx, 0.6f, kRoofDark);
    boxWindows(mb, mn, mx, 3, 0.18f, 0.04f, 7, 5, kWindow, 0.6f, 0.55f);
    tallChimney(mb, 0.78f, -0.6f, 1.4f, kBrick);
    door(mb, -0.2f, mx.z, 0.14f, 0.18f);
}

void sawmill(MeshBuilder& mb, u8) {
    // An open-sided shed over the saw line, logs in and planks out.
    const glm::vec3 mn(-0.75f, 0, -0.35f), mx(0.75f, 0.36f, 0.25f);
    for (const float x : {mn.x, -0.25f, 0.25f, mx.x - 0.04f})
        for (const float z : {mn.z, mx.z - 0.04f}) mb.box({x, 0, z}, {x + 0.04f, mx.y, z + 0.04f}, kTimber);
    mb.gableRoofX(mn, mx, 0.36f, 0.18f, rgba(120, 120, 124), kTimber, 0.06f);
    mb.box({-0.7f, 0, -0.08f}, {0.7f, 0.1f, 0.02f}, kSteelGrey);
    logPile(mb, -0.45f, -0.75f, 0.7f, 2);
    plankStack(mb, -0.4f, 0.65f);
    plankStack(mb, 0.1f, 0.65f);
    plankStack(mb, 0.55f, 0.6f);
}

void brickworks(MeshBuilder& mb, u8) {
    // A long kiln with a tall chimney, and stacks of bricks.
    const glm::vec3 mn(-0.85f, 0, -0.7f), mx(0.4f, 0.32f, -0.05f);
    walls(mb, mn, mx, kBrick);
    mb.gableRoofX(mn, mx, 0.32f, 0.12f, kRoofDark, kBrick, 0.02f);
    tallChimney(mb, 0.65f, -0.45f, 1.5f, kBrick);
    for (int i = 0; i < 4; ++i) {
        const float x = -0.7f + i * 0.38f;
        mb.box({x, 0, 0.25f}, {x + 0.26f, 0.18f, 0.6f}, i % 2 ? kBrick : shade(kBrick, 0.88f));
    }
}

void steelMill(MeshBuilder& mb, u8) {
    // Blast furnace towers, a rolling mill hall and chimneys.
    const glm::vec3 mn(-1.4f, 0, 0.1f), mx(1.4f, 0.55f, 1.35f);
    walls(mb, mn, mx, rgba(120, 124, 132));
    sawtoothRoof(mb, mn, mx, 0.55f, 7, 0.2f, kRoofDark, kGlassDark);
    for (const float x : {-0.9f, -0.2f}) {
        mb.frustum({x, 0, -0.7f}, 0.28f, 0.2f, 1.1f, 10, rgba(86, 90, 98));
        mb.prism({x, 1.1f, -0.7f}, 0.12f, 0.35f, 8, rgba(70, 74, 80), true);
        mb.box({x - 0.04f, 0.6f, -0.45f}, {x + 0.04f, 0.66f, 0.1f}, kSteelGrey);
    }
    for (const float x : {0.6f, 1.0f}) tallChimney(mb, x, -0.9f, 1.8f, rgba(150, 70, 56));
    tank(mb, 0.8f, -0.25f, 0.2f, 0.45f, kSteelGrey);
}

void goodsFactory(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-1.4f, 0, -1.2f), mx(0.9f, 0.6f, 0.9f);
    walls(mb, mn, mx, kWall);
    sawtoothRoof(mb, mn, mx, 0.6f, 6, 0.24f, kRoofSlate, kGlassDark);
    const glm::vec3 omn(0.9f, 0, 0.1f), omx(1.4f, 0.7f, 0.9f);
    walls(mb, omn, omx, kConcrete);
    flatRoof(mb, omn, omx, 0.7f, kConcreteDark);
    boxWindows(mb, omn, omx, 2, 0.3f, 0.05f, 2, 3);
    tallChimney(mb, 1.15f, -0.8f, 1.7f, kRed);
    for (const float x : {-1.0f, -0.5f, 0.0f})
        mb.quadFacing({x, 0, mx.z + kInset}, {x + 0.35f, 0, mx.z + kInset}, {x + 0.35f, 0.36f, mx.z + kInset}, {x, 0.36f, mx.z + kInset},
                      kConcreteDark, {0, 0, 1});
}

void furnitureFactory(MeshBuilder& mb, u8) {
    const glm::vec3 mn(-0.85f, 0, -0.75f), mx(0.45f, 0.45f, 0.45f);
    walls(mb, mn, mx, kTimber);
    sawtoothRoof(mb, mn, mx, 0.45f, 4, 0.18f, kRoofDark, kGlassDark);
    windowRow(mb, {mn.x, 0, mx.z}, {mx.x, 0, mx.z}, 0.16f, 0.32f, 4, {0, 0, 1});
    plankStack(mb, 0.68f, -0.5f);
    plankStack(mb, 0.68f, 0.0f);
    door(mb, -0.3f, mx.z, 0.16f, 0.26f);
    chimney(mb, -0.6f, -0.5f, 0.6f, 0.25f);
}

// Add separate paddocks, stores and work sheds around the original production building.
// The uneven arrangement leaves lanes and open corners instead of a single square block.
void industryGrounds(MeshBuilder& mb, u16 type, u8 variant) {
    const float half = buildingType(type).size * 0.5f;
    const bool farm = type >= 15 && type <= 17;
    const bool timber = type == 18 || type == 24 || type == 28;
    const float edge = half + 0.85f;
    for (int section = 0; section <= variant; ++section) {
        const float z = -half + section * (2.0f * half / 3.0f);
        if (farm) {
            if (type == 15) {
                field(mb, -edge, z, -half - 0.12f, z + 0.85f, section % 2 ? rgba(126, 168, 72) : rgba(214, 186, 92), rgba(166, 148, 76), 5);
            } else {
                field(mb, -edge, z, -half - 0.12f, z + 0.85f, kPasture, kPasture, 1);
                fence(mb, -edge, z, -half - 0.12f, z + 0.85f);
                if (type == 16) {
                    sheep(mb, -half - 0.5f, z + 0.22f, 0.7f);
                    sheep(mb, -half - 0.38f, z + 0.6f, 2.0f);
                } else cow(mb, -half - 0.5f, z + 0.4f, section % 2 == 0);
            }
        } else {
            mb.box({-edge, 0, z}, {-half - 0.1f, 0.015f, z + 0.7f}, rgba(144, 132, 110));
            if (timber) logPile(mb, -half - 0.48f, z + 0.35f, 0.55f, 2 + section);
            else heap(mb, -half - 0.45f, z + 0.3f, 0.24f, 0.2f + section * 0.06f, type == 21 ? rgba(48, 48, 52) : kRockGrey);
        }
    }
    // A maintenance shed, loading store and, at larger sites, a second workshop.
    for (int i = 0; i < 1 + variant; ++i) {
        const float x = -half + 0.15f + i * 0.8f;
        const glm::vec3 mn(x, 0, half + 0.18f), mx(x + 0.58f, 0.28f + i * 0.05f, edge - 0.08f);
        const u32 wall = farm ? kBarnRed : (timber ? kTimber : kBrick);
        walls(mb, mn, mx, wall);
        mb.gableRoofX(mn, mx, mx.y, 0.16f, kRoofSlate, wall, 0.025f);
        door(mb, x + 0.2f, mx.z, 0.15f, 0.22f);
    }
    if (variant > 0 && farm) silo(mb, half + 0.4f, -half + 0.35f, 0.18f, 0.65f + variant * 0.15f);
    if (variant > 0 && timber) {
        plankStack(mb, half + 0.4f, 0.2f);
        plankStack(mb, half + 0.4f, 0.7f);
    }
}

} // namespace

void buildBuildingModel(MeshBuilder& mb, u16 type, u8 variant) {
    switch (type) {
    case 0: cottage(mb, variant); break;
    case 1: house(mb, variant); break;
    case 2: terrace(mb, variant); break;
    case 3: apartments(mb, variant); break;
    case 4: towerBlock(mb, variant); break;
    case 5: cornerShop(mb, variant); break;
    case 6: highStreet(mb, variant); break;
    case 7: departmentStore(mb, variant); break;
    case 8: smallOffice(mb, variant); break;
    case 9: officeTower(mb, variant); break;
    case 10: workshop(mb, variant); break;
    case 11: factory(mb, variant); break;
    case 12: farm(mb, variant); break;
    case 13: church(mb, variant); break;
    case 14: school(mb, variant); break;
    case 15: grainFarm(mb, variant); break;
    case 16: sheepFarm(mb, variant); break;
    case 17: dairyFarm(mb, variant); break;
    case 18: loggingCamp(mb, variant); break;
    case 19: quarry(mb, variant); break;
    case 20: ironMine(mb, variant); break;
    case 21: coalMine(mb, variant); break;
    case 22: cheeseDairy(mb, variant); break;
    case 23: textileMill(mb, variant); break;
    case 24: sawmill(mb, variant); break;
    case 25: brickworks(mb, variant); break;
    case 26: steelMill(mb, variant); break;
    case 27: goodsFactory(mb, variant); break;
    case 28: furnitureFactory(mb, variant); break;
    default: house(mb, 0); break;
    }
    if (type >= 15 && type <= 28) industryGrounds(mb, type, variant);
}

} // namespace tp::gfx
