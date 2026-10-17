// Procedural low poly town buildings. Every model is centred on its footprint with the entrance
// facing +Z; walls use tintable colours so instances can vary their facade colour.

#include "render/models.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace tp::gfx {

namespace {

const u32 kWall = tintable(1.0f);
const u32 kWallShade = tintable(0.86f);
constexpr u32 kRoofTile = rgba(166, 78, 62);
constexpr u32 kRoofSlate = rgba(80, 86, 98);
constexpr u32 kRoofThatch = rgba(196, 166, 96);
constexpr u32 kRoofDark = rgba(70, 62, 60);
constexpr u32 kWindow = rgba(52, 68, 90);
constexpr u32 kDoor = rgba(98, 64, 42);
constexpr u32 kConcrete = rgba(182, 180, 172);
constexpr u32 kConcreteDark = rgba(140, 138, 132);
constexpr u32 kGlass = rgba(96, 142, 178);
constexpr u32 kGlassDark = rgba(62, 96, 132);
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
    default: house(mb, 0); break;
    }
}

} // namespace tp::gfx
