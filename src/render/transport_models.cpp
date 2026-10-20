// Procedural low poly models for vehicles, stops and depots. Vehicles face +X; tintable parts
// take the owning company's colour.

#include "render/models.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace tp::gfx {

namespace {

const u32 kLivery = tintable(1.0f);
const u32 kLiveryDark = tintable(0.75f);
constexpr u32 kWhite = rgba(236, 236, 230);
constexpr u32 kGlass = rgba(58, 78, 100, 254); // lit from inside at night
constexpr u32 kTyre = rgba(34, 34, 38);
constexpr u32 kChrome = rgba(170, 172, 176);
constexpr u32 kRoofGrey = rgba(120, 124, 130);
constexpr u32 kLight = rgba(250, 236, 170, 252);    // headlamp
constexpr u32 kTailLamp = rgba(200, 40, 30, 251);
constexpr float kInset = 0.004f;
// Tank engine running gear, shared with the renderer's animation (see vehicle_fx.cpp).
constexpr float kSteamWheelRadius = 0.052f;
constexpr float kSteamCrank = 0.022f;
constexpr float kSteamCrossheadY = kSteamWheelRadius + 0.016f;

void wheels(MeshBuilder& mb, float x0, float x1, float halfW, float r = 0.045f) {
    for (const float x : {x0, x1}) {
        mb.box({x - r, 0.0f, halfW - 0.035f}, {x + r, r * 2.0f, halfW + 0.005f}, kTyre);
        mb.box({x - r, 0.0f, -halfW - 0.005f}, {x + r, r * 2.0f, -halfW + 0.035f}, kTyre);
    }
}

// A pair of lamps on the end of a vehicle at x (facing +X when front, -X at the back).
void lamps(MeshBuilder& mb, float x, float y, float spread, bool front) {
    const float x0 = front ? x : x - 0.012f, x1 = front ? x + 0.012f : x;
    for (const float z : {-spread, spread})
        mb.box({x0, y - 0.012f, z - 0.014f}, {x1, y + 0.012f, z + 0.014f}, front ? kLight : kTailLamp);
}

// Side window band from x0 to x1 at heights y0..y1 on both sides.
void windowBand(MeshBuilder& mb, float x0, float x1, float y0, float y1, float halfW) {
    mb.quadFacing({x0, y0, halfW + kInset}, {x1, y0, halfW + kInset}, {x1, y1, halfW + kInset}, {x0, y1, halfW + kInset},
                  kGlass, {0, 0, 1});
    mb.quadFacing({x0, y0, -halfW - kInset}, {x1, y0, -halfW - kInset}, {x1, y1, -halfW - kInset},
                  {x0, y1, -halfW - kInset}, kGlass, {0, 0, -1});
}

void busBody(MeshBuilder& mb, float length, float height, bool slantedFront, bool articulated) {
    const float hl = length * 0.5f, hw = 0.1f, base = 0.045f;
    const float top = base + height;
    // Body
    mb.box({-hl, base, -hw}, {hl - (slantedFront ? 0.05f : 0.0f), top, hw}, kLivery);
    if (slantedFront) {
        // Rounded / slanted nose.
        const float x0 = hl - 0.05f;
        mb.quadFacing({x0, base, -hw}, {hl, base, -hw}, {hl, top - 0.06f, -hw}, {x0, top, -hw}, kLivery, {0, 0, -1});
        mb.quadFacing({x0, base, hw}, {x0, top, hw}, {hl, top - 0.06f, hw}, {hl, base, hw}, kLivery, {0, 0, 1});
        mb.quadFacing({hl, base, -hw}, {hl, base, hw}, {hl, top - 0.06f, hw}, {hl, top - 0.06f, -hw}, kLivery, {1, 0, 0});
        mb.quadFacing({x0, top, -hw}, {hl, top - 0.06f, -hw}, {hl, top - 0.06f, hw}, {x0, top, hw}, kGlass, {1, 1, 0});
    } else {
        mb.quadFacing({hl + kInset, base + height * 0.45f, -hw + 0.015f}, {hl + kInset, base + height * 0.45f, hw - 0.015f},
                      {hl + kInset, top - 0.02f, hw - 0.015f}, {hl + kInset, top - 0.02f, -hw + 0.015f}, kGlass,
                      {1, 0, 0});
    }
    // Roof and window band.
    mb.box({-hl + 0.02f, top, -hw + 0.015f}, {hl - 0.08f, top + 0.012f, hw - 0.015f}, kWhite);
    windowBand(mb, -hl + 0.04f, hl - 0.09f, base + height * 0.45f, top - 0.025f, hw);
    // Headlights.
    mb.box({hl - 0.004f, base + 0.03f, -hw + 0.02f}, {hl + 0.004f, base + 0.05f, -hw + 0.05f}, kLight);
    mb.box({hl - 0.004f, base + 0.03f, hw - 0.05f}, {hl + 0.004f, base + 0.05f, hw - 0.02f}, kLight);
    if (articulated) {
        mb.box({-0.02f, base, -hw - kInset}, {0.02f, top, hw + kInset}, kRoofGrey);
        wheels(mb, -hl + 0.09f, 0.0f, hw);
        wheels(mb, hl - 0.1f, hl - 0.1f, hw);
    } else {
        wheels(mb, -hl + 0.1f, hl - 0.11f, hw);
    }
}

void tramBody(MeshBuilder& mb, float length, int sections) {
    const float hl = length * 0.5f, hw = 0.1f, base = 0.03f, top = 0.27f;
    const float seg = length / static_cast<float>(sections);
    for (int i = 0; i < sections; ++i) {
        const float x0 = -hl + seg * static_cast<float>(i) + (i ? 0.012f : 0.0f);
        const float x1 = -hl + seg * static_cast<float>(i + 1) - (i + 1 < sections ? 0.012f : 0.0f);
        mb.box({x0, base, -hw}, {x1, top, hw}, kLivery);
        mb.box({x0 + 0.01f, base, -hw - kInset}, {x1 - 0.01f, base + 0.05f, hw + kInset}, kLiveryDark);
        windowBand(mb, x0 + 0.03f, x1 - 0.03f, base + 0.1f, top - 0.03f, hw);
        mb.box({x0 + 0.02f, top, -hw + 0.02f}, {x1 - 0.02f, top + 0.015f, hw - 0.02f}, kRoofGrey);
    }
    for (int i = 0; i + 1 < sections; ++i) {
        const float x = -hl + seg * static_cast<float>(i + 1);
        mb.box({x - 0.014f, base + 0.02f, -hw + 0.02f}, {x + 0.014f, top - 0.02f, hw - 0.02f}, kRoofGrey);
    }
    // Pantograph.
    const float px = -hl + seg * 0.5f;
    mb.quadFacing({px - 0.06f, top + 0.015f, 0.0f}, {px, top + 0.09f, -0.004f}, {px + 0.06f, top + 0.015f, 0.0f},
                  {px, top + 0.09f, 0.004f}, kChrome, {0, 0, 1});
    mb.box({px - 0.05f, top + 0.088f, -0.04f}, {px + 0.05f, top + 0.096f, 0.04f}, kChrome);
    // Cab glass at both ends.
    mb.quadFacing({hl + kInset, base + 0.1f, -hw + 0.015f}, {hl + kInset, base + 0.1f, hw - 0.015f},
                  {hl + kInset, top - 0.03f, hw - 0.015f}, {hl + kInset, top - 0.03f, -hw + 0.015f}, kGlass, {1, 0, 0});
    mb.quadFacing({-hl - kInset, base + 0.1f, -hw + 0.015f}, {-hl - kInset, top - 0.03f, -hw + 0.015f},
                  {-hl - kInset, top - 0.03f, hw - 0.015f}, {-hl - kInset, base + 0.1f, hw - 0.015f}, kGlass,
                  {-1, 0, 0});
}

void busStopShelter(MeshBuilder& mb) {
    // Shelter on the pavement: open towards +Z (the road), sign pole in company colours.
    constexpr u32 kFrame = rgba(70, 74, 80);
    constexpr u32 kPane = rgba(150, 190, 210);
    mb.box({-0.16f, 0.0f, -0.05f}, {0.16f, 0.22f, -0.04f}, kPane);
    mb.box({-0.17f, 0.22f, -0.06f}, {0.17f, 0.235f, 0.06f}, kFrame);
    mb.box({-0.17f, 0.0f, -0.055f}, {-0.155f, 0.22f, -0.035f}, kFrame);
    mb.box({0.155f, 0.0f, -0.055f}, {0.17f, 0.22f, -0.035f}, kFrame);
    mb.box({-0.12f, 0.06f, -0.04f}, {0.12f, 0.075f, 0.0f}, rgba(140, 100, 70));
    mb.box({0.2f, 0.0f, 0.0f}, {0.212f, 0.3f, 0.012f}, kFrame);
    mb.box({0.18f, 0.25f, -0.005f}, {0.232f, 0.31f, 0.017f}, kLivery);
}

void depotBuilding(MeshBuilder& mb, bool tram) {
    constexpr u32 kBrick = rgba(170, 120, 96);
    constexpr u32 kDoor = rgba(64, 66, 70);
    const glm::vec3 mn(-0.42f, 0.0f, -0.42f), mx(0.42f, 0.42f, 0.42f);
    mb.box(mn, mx, kBrick, 0x3F & ~0x08);
    // Roof ridge runs along Z (front to back).
    const glm::mat4 saved = mb.transform;
    mb.transform = saved * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    mb.gableRoofX({-mx.z, 0, mn.x}, {-mn.z, 0, mx.x}, 0.42f, 0.2f, rgba(84, 90, 100), kBrick, 0.03f);
    mb.transform = saved;
    // Big door facing +Z and a company coloured band.
    mb.quadFacing({-0.3f, 0.0f, mx.z + kInset}, {0.3f, 0.0f, mx.z + kInset}, {0.3f, 0.32f, mx.z + kInset},
                  {-0.3f, 0.32f, mx.z + kInset}, kDoor, {0, 0, 1});
    mb.box({-0.43f, 0.34f, mx.z - 0.01f}, {0.43f, 0.39f, mx.z + 0.012f}, kLivery);
    if (tram) mb.box({-0.02f, 0.0f, mx.z - 0.01f}, {0.02f, 0.32f, mx.z + 0.01f}, kChrome);
}

// Railway vehicles: length along X in tiles (matching the simulation's car length).
constexpr u32 kEngineBlack = rgba(38, 40, 44);
constexpr u32 kSmokebox = rgba(48, 48, 52);
constexpr u32 kBufferRed = rgba(168, 42, 34);
constexpr u32 kBrass = rgba(214, 170, 72);
constexpr u32 kCoal = rgba(26, 26, 28);
constexpr u32 kSteel = rgba(150, 152, 158);

// A cylinder of `length` along +X from (x, y, z) (prism axis turned from +Y).
void cylinderX(MeshBuilder& mb, float x, float y, float z, float radius, float length, int sides, u32 colour) {
    const glm::mat4 saved = mb.transform;
    mb.transform = saved * glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)) *
                   glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1));
    mb.prism({0.0f, 0.0f, 0.0f}, radius, length, sides, colour, true);
    mb.transform = saved;
}

// A disc of `thickness` along +Z from z, centred on (x, y).
void discZ(MeshBuilder& mb, float x, float y, float z, float radius, float thickness, int sides, u32 colour) {
    const glm::mat4 saved = mb.transform;
    mb.transform = saved * glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)) *
                   glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    mb.prism({0.0f, 0.0f, 0.0f}, radius, thickness, sides, colour, true);
    mb.transform = saved;
}

// Tank engine (0-6-0T): boiler with brass bands and dome, smokebox and chimney, side tanks, cab and
// coal bunker, cylinders and slide bars. Its driving wheels and rods are separate models
// (kSteamWheelset, kSideRod, kMainRod) that the renderer turns as the engine runs.
void steamEngine(MeshBuilder& mb) {
    const float hl = 0.31f, hw = 0.1f;
    // Frames, footplate with its red valance, buffer beams, buffers and couplings.
    mb.box({-hl, 0.07f, -hw - 0.005f}, {hl, 0.085f, hw + 0.005f}, kEngineBlack);
    for (const float side : {-1.0f, 1.0f}) {
        mb.box({-0.27f, 0.026f, side * 0.07f - 0.006f}, {0.27f, 0.07f, side * 0.07f + 0.006f}, kEngineBlack);
        mb.box({-hl, 0.062f, side * (hw + 0.002f) - 0.004f}, {hl, 0.072f, side * (hw + 0.002f) + 0.004f}, kBufferRed);
    }
    for (const float end : {-1.0f, 1.0f}) {
        const float x = end * hl;
        mb.box({std::min(x, x - end * 0.012f), 0.04f, -hw}, {std::max(x, x - end * 0.012f), 0.088f, hw}, kBufferRed);
        for (const float z : {-0.068f, 0.068f}) {
            cylinderX(mb, end > 0 ? x : x - 0.024f, 0.062f, z, 0.008f, 0.024f, 6, kSteel);
            cylinderX(mb, end > 0 ? x + 0.024f : x - 0.03f, 0.062f, z, 0.016f, 0.006f, 8, kEngineBlack);
        }
        mb.box({std::min(x, x + end * 0.022f), 0.055f, -0.006f}, {std::max(x, x + end * 0.022f), 0.068f, 0.006f}, kSteel);
    }
    // Boiler, bands, smokebox and its door, saddle and chimney.
    cylinderX(mb, -0.07f, 0.165f, 0.0f, 0.066f, 0.28f, 12, kLivery);
    for (const float x : {-0.01f, 0.06f, 0.13f}) cylinderX(mb, x, 0.165f, 0.0f, 0.0675f, 0.007f, 12, kBrass);
    cylinderX(mb, 0.205f, 0.165f, 0.0f, 0.07f, 0.07f, 12, kSmokebox);
    cylinderX(mb, 0.275f, 0.165f, 0.0f, 0.058f, 0.006f, 12, kEngineBlack);
    mb.box({0.281f, 0.12f, -0.004f}, {0.286f, 0.2f, 0.004f}, kSteel);
    mb.box({0.2f, 0.085f, -0.05f}, {0.27f, 0.12f, 0.05f}, kSmokebox);
    mb.frustum({0.24f, 0.212f, 0.0f}, 0.034f, 0.021f, 0.022f, 10, kSmokebox);
    mb.prism({0.24f, 0.232f, 0.0f}, 0.021f, 0.054f, 10, kEngineBlack, true);
    mb.prism({0.24f, 0.282f, 0.0f}, 0.027f, 0.013f, 10, kEngineBlack, true);
    mb.prism({0.24f, 0.2952f, 0.0f}, 0.017f, 0.001f, 10, kCoal, true);
    // Dome, safety valves and whistle; a handrail along each side of the boiler.
    mb.prism({0.075f, 0.212f, 0.0f}, 0.028f, 0.032f, 10, kBrass, true);
    mb.icosphere({0.075f, 0.244f, 0.0f}, 0.028f, kBrass, 1, 0.45f);
    mb.prism({-0.035f, 0.222f, 0.0f}, 0.012f, 0.03f, 8, kBrass, true);
    mb.prism({-0.055f, 0.228f, 0.0f}, 0.005f, 0.04f, 6, kBrass, true);
    for (const float side : {-1.0f, 1.0f})
        mb.box({-0.06f, 0.198f, side * 0.074f - 0.003f}, {0.25f, 0.204f, side * 0.074f + 0.003f}, kSteel);
    // Side tanks with lining and filler caps.
    constexpr u32 kLining = rgba(232, 204, 124);
    for (const float side : {-1.0f, 1.0f}) {
        const float inner = side * 0.046f, outer = side * 0.104f;
        mb.box({-0.06f, 0.085f, std::min(inner, outer)}, {0.175f, 0.205f, std::max(inner, outer)}, kLivery);
        mb.box({-0.062f, 0.205f, std::min(inner, outer) - 0.002f}, {0.177f, 0.211f, std::max(inner, outer) + 0.002f}, kLiveryDark);
        const float face = outer + side * 0.0015f;
        const auto line = [&](float x0, float y0, float x1, float y1) {
            mb.box({x0, y0, face - 0.0012f}, {x1, y1, face + 0.0012f}, kLining);
        };
        line(-0.045f, 0.105f, 0.16f, 0.109f);
        line(-0.045f, 0.187f, 0.16f, 0.191f);
        line(-0.045f, 0.105f, -0.041f, 0.191f);
        line(0.156f, 0.105f, 0.16f, 0.191f);
        mb.prism({0.12f, 0.211f, side * 0.075f}, 0.012f, 0.008f, 8, kEngineBlack, true);
    }
    // Cab: lower sides as high as the tanks, front plate with spectacle windows, side openings
    // glowing from the firebox at night, and an arched roof.
    mb.box({-0.225f, 0.085f, -0.104f}, {-0.06f, 0.205f, 0.104f}, kLivery);
    mb.box({-0.077f, 0.205f, -0.104f}, {-0.06f, 0.305f, 0.104f}, kLivery);
    mb.box({-0.225f, 0.205f, -0.104f}, {-0.207f, 0.305f, 0.104f}, kLivery);
    for (const float side : {-1.0f, 1.0f}) {
        mb.box({-0.207f, 0.285f, side * 0.104f - 0.008f}, {-0.077f, 0.305f, side * 0.104f + 0.008f}, kLivery);
        mb.quadFacing({-0.0595f, 0.248f, side * 0.042f}, {-0.0595f, 0.248f, side * 0.088f}, {-0.0595f, 0.288f, side * 0.088f},
                      {-0.0595f, 0.288f, side * 0.042f}, kGlass, {1, 0, 0});
    }
    mb.box({-0.207f, 0.205f, -0.096f}, {-0.077f, 0.285f, 0.096f}, rgba(44, 34, 28));
    windowBand(mb, -0.205f, -0.079f, 0.215f, 0.283f, 0.1f);
    mb.box({-0.235f, 0.305f, -0.113f}, {-0.05f, 0.316f, 0.113f}, kEngineBlack);
    mb.box({-0.228f, 0.316f, -0.082f}, {-0.057f, 0.325f, 0.082f}, kEngineBlack);
    // Coal bunker heaped with coal.
    mb.box({-hl + 0.004f, 0.085f, -0.1f}, {-0.225f, 0.25f, 0.1f}, kLivery);
    mb.box({-hl + 0.004f, 0.25f, -0.102f}, {-0.225f, 0.256f, 0.102f}, kLiveryDark);
    mb.box({-0.298f, 0.256f, -0.085f}, {-0.232f, 0.27f, 0.085f}, kCoal);
    mb.box({-0.286f, 0.27f, -0.05f}, {-0.246f, 0.281f, 0.035f}, kCoal);
    // Cylinders with steel covers, and the slide bars the crossheads run in.
    for (const float side : {-1.0f, 1.0f}) {
        cylinderX(mb, 0.17f, kSteamCrossheadY, side * 0.1f, 0.026f, 0.085f, 10, kEngineBlack);
        cylinderX(mb, 0.164f, kSteamCrossheadY, side * 0.1f, 0.021f, 0.006f, 10, kSteel);
        cylinderX(mb, 0.255f, kSteamCrossheadY, side * 0.1f, 0.021f, 0.006f, 10, kSteel);
        for (const float y : {kSteamCrossheadY + 0.012f, kSteamCrossheadY - 0.012f})
            mb.box({0.11f, y - 0.003f, side * 0.121f - 0.003f}, {0.17f, y + 0.003f, side * 0.121f + 0.003f}, kSteel);
    }
    lamps(mb, hl + 0.008f, 0.1f, 0.06f, true);
    mb.box({0.262f, 0.236f, -0.012f}, {0.284f, 0.26f, 0.012f}, kLight); // lamp on the smokebox
    lamps(mb, -hl, 0.1f, 0.06f, false);
}

// One driving axle of the tank engine, centred on the axle: steel tyres, red centres with spokes
// and a counterweight, and the crank pins (the left a quarter turn ahead of the right).
void steamWheelset(MeshBuilder& mb) {
    constexpr float r = kSteamWheelRadius;
    constexpr u32 kTyreSteel = rgba(52, 52, 56);
    mb.box({-0.008f, -0.008f, -0.09f}, {0.008f, 0.008f, 0.09f}, kEngineBlack);
    for (const float side : {-1.0f, 1.0f}) {
        const float z0 = side > 0 ? 0.084f : -0.1f;
        discZ(mb, 0.0f, 0.0f, z0, r, 0.016f, 14, kTyreSteel);
        const float face = side > 0 ? 0.1f : -0.1015f;
        discZ(mb, 0.0f, 0.0f, face, r * 0.84f, 0.0015f, 14, kBufferRed);
        const float spokeZ = side > 0 ? face + 0.0015f : face - 0.001f;
        const glm::mat4 saved = mb.transform;
        for (int k = 0; k < 8; ++k) {
            mb.transform = saved * glm::rotate(glm::mat4(1.0f), glm::radians(45.0f * k), glm::vec3(0, 0, 1));
            mb.box({0.009f, -0.0025f, spokeZ}, {r * 0.8f, 0.0025f, spokeZ + 0.001f}, kEngineBlack);
        }
        // Counterweight opposite the crank.
        mb.transform = saved * glm::rotate(glm::mat4(1.0f), side > 0 ? 0.0f : glm::radians(90.0f), glm::vec3(0, 0, 1));
        mb.box({-r * 0.8f, -0.014f, spokeZ}, {-0.012f, 0.014f, spokeZ + 0.0012f}, kEngineBlack);
        mb.transform = saved;
        discZ(mb, 0.0f, 0.0f, side > 0 ? face : face - 0.003f, 0.011f, 0.003f, 8, kSteel);
        const glm::vec2 pin = side > 0 ? glm::vec2(kSteamCrank, 0.0f) : glm::vec2(0.0f, kSteamCrank);
        discZ(mb, pin.x, pin.y, side > 0 ? face : face - 0.008f, 0.006f, 0.008f, 8, kSteel);
    }
}

// Coupling rod of one side, in the engine's own X (the axles at -0.15, -0.02 and 0.11).
void steamSideRod(MeshBuilder& mb) {
    mb.box({-0.162f, -0.006f, -0.003f}, {0.122f, 0.006f, 0.003f}, kSteel);
    for (const float x : {-0.15f, -0.02f, 0.11f}) discZ(mb, x, 0.0f, -0.0035f, 0.01f, 0.007f, 8, kSteel);
}

// Connecting rod, one tile long along +X (scaled to length), with the crosshead at its far end.
void steamMainRod(MeshBuilder& mb) {
    mb.box({0.0f, -0.0065f, -0.003f}, {1.0f, 0.0065f, 0.003f}, kSteel);
    mb.box({0.94f, -0.013f, -0.006f}, {1.06f, 0.013f, 0.006f}, kSteel);
}

void dieselEngine(MeshBuilder& mb, bool express) {
    constexpr u32 kGrey = rgba(70, 74, 80);
    const float hl = express ? 0.4f : 0.37f, hw = 0.105f, base = 0.05f, top = 0.28f;
    mb.box({-hl, base, -hw}, {hl, base + 0.03f, hw}, kGrey);
    mb.box({-hl + 0.02f, base + 0.03f, -hw}, {hl - 0.06f, top, hw}, kLivery);
    // Sloped cab front.
    mb.quadFacing({hl - 0.06f, base + 0.03f, -hw}, {hl, base + 0.03f, -hw}, {hl, top - 0.07f, -hw}, {hl - 0.06f, top, -hw},
                  kLivery, {0, 0, -1});
    mb.quadFacing({hl - 0.06f, base + 0.03f, hw}, {hl - 0.06f, top, hw}, {hl, top - 0.07f, hw}, {hl, base + 0.03f, hw},
                  kLivery, {0, 0, 1});
    mb.quadFacing({hl, base + 0.03f, -hw}, {hl, base + 0.03f, hw}, {hl, top - 0.07f, hw}, {hl, top - 0.07f, -hw}, kLiveryDark,
                  {1, 0, 0});
    mb.quadFacing({hl - 0.06f, top, -hw}, {hl, top - 0.07f, -hw}, {hl, top - 0.07f, hw}, {hl - 0.06f, top, hw}, kGlass,
                  {1, 1, 0});
    mb.box({-hl + 0.04f, top, -hw + 0.02f}, {hl - 0.1f, top + 0.015f, hw - 0.02f}, kRoofGrey);
    mb.box({-hl + 0.02f, base + 0.04f, -hw - kInset}, {hl - 0.06f, base + 0.07f, hw + kInset}, kWhite); // stripe
    windowBand(mb, hl - 0.16f, hl - 0.08f, base + 0.13f, top - 0.04f, hw);
    lamps(mb, hl, base + 0.06f, 0.065f, true);
    lamps(mb, -hl + 0.02f, base + 0.06f, 0.065f, false);
    wheels(mb, -hl + 0.12f, hl - 0.14f, hw, 0.045f);
    if (express) wheels(mb, -0.04f, 0.04f, hw, 0.045f);
}

void railcar(MeshBuilder& mb, float length, bool powered, bool wooden) {
    constexpr u32 kWood = rgba(122, 70, 44);
    const float hl = length * 0.5f, hw = 0.1f, base = 0.06f, top = 0.27f;
    const u32 body = wooden ? kWood : kLivery;
    mb.box({-hl, base, -hw}, {hl, top, hw}, body);
    mb.box({-hl + 0.01f, base, -hw - kInset}, {hl - 0.01f, base + 0.035f, hw + kInset}, wooden ? kLiveryDark : kLiveryDark);
    windowBand(mb, -hl + 0.05f, hl - 0.05f, base + 0.09f, top - 0.04f, hw);
    // Curved-ish roof.
    mb.box({-hl + 0.005f, top, -hw + 0.015f}, {hl - 0.005f, top + 0.02f, hw - 0.015f}, kRoofGrey);
    mb.box({-hl + 0.01f, top + 0.02f, -hw + 0.04f}, {hl - 0.01f, top + 0.03f, hw - 0.04f}, kRoofGrey);
    if (powered) {
        mb.quadFacing({hl + kInset, base + 0.09f, -hw + 0.015f}, {hl + kInset, base + 0.09f, hw - 0.015f},
                      {hl + kInset, top - 0.03f, hw - 0.015f}, {hl + kInset, top - 0.03f, -hw + 0.015f}, kGlass, {1, 0, 0});
        mb.box({hl - 0.004f, base + 0.04f, -hw + 0.02f}, {hl + 0.004f, base + 0.06f, -hw + 0.05f}, kLight);
        mb.box({hl - 0.004f, base + 0.04f, hw - 0.05f}, {hl + 0.004f, base + 0.06f, hw - 0.02f}, kLight);
        lamps(mb, -hl, base + 0.05f, 0.065f, false);
    }
    wheels(mb, -hl + 0.1f, hl - 0.1f, hw, 0.04f);
}

void railDepotBuilding(MeshBuilder& mb) {
    constexpr u32 kBrick = rgba(150, 104, 84);
    constexpr u32 kDoor = rgba(54, 56, 60);
    const glm::vec3 mn(-0.44f, 0.0f, -0.44f), mx(0.44f, 0.5f, 0.44f);
    mb.box(mn, mx, kBrick, 0x3F & ~0x08);
    const glm::mat4 saved = mb.transform;
    mb.transform = saved * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    mb.gableRoofX({-mx.z, 0, mn.x}, {-mn.z, 0, mx.x}, 0.5f, 0.18f, rgba(70, 76, 86), kBrick, 0.03f);
    mb.transform = saved;
    mb.quadFacing({-0.22f, 0.0f, mx.z + kInset}, {0.22f, 0.0f, mx.z + kInset}, {0.22f, 0.38f, mx.z + kInset},
                  {-0.22f, 0.38f, mx.z + kInset}, kDoor, {0, 0, 1});
    mb.box({-0.45f, 0.42f, mx.z - 0.01f}, {0.45f, 0.46f, mx.z + 0.012f}, kLivery);
    mb.prism({0.3f, 0.5f, -0.25f}, 0.04f, 0.25f, 6, rgba(120, 90, 76), true); // chimney
}

void signalPost(MeshBuilder& mb) {
    constexpr u32 kPost = rgba(60, 62, 66);
    mb.box({-0.014f, 0.0f, -0.014f}, {0.014f, 0.34f, 0.014f}, kPost);
    mb.box({-0.035f, 0.26f, -0.03f}, {0.035f, 0.4f, 0.03f}, rgba(30, 30, 32));
    // Lamp facing +X (towards the approaching train): tintable red / green, with a white backplate.
    mb.box({0.035f, 0.27f, -0.045f}, {0.04f, 0.39f, 0.045f}, rgba(230, 230, 226));
    mb.box({0.04f, 0.31f, -0.024f}, {0.05f, 0.36f, 0.024f}, tintable(1.0f));
}

void platformCanopy(MeshBuilder& mb) {
    // Two platform canopies along X, one on each side of the track.
    constexpr u32 kPost = rgba(96, 100, 108);
    constexpr u32 kRoof = rgba(150, 154, 160);
    for (const float z : {-0.37f, 0.37f}) {
        mb.box({-0.5f, 0.42f, z - 0.12f}, {0.5f, 0.44f, z + 0.12f}, kRoof);
        mb.box({-0.02f, 0.12f, z - 0.015f}, {0.02f, 0.42f, z + 0.015f}, kPost);
        mb.box({-0.08f, 0.12f, z - 0.02f}, {0.08f, 0.17f, z + 0.02f}, rgba(120, 90, 60)); // bench
    }
}

// Ships: the model origin sits on the water line, the hull points along +X.
constexpr u32 kDeck = rgba(176, 140, 96);
constexpr u32 kHullBottom = rgba(120, 40, 36);
constexpr u32 kPaint = rgba(240, 240, 234);

void hull(MeshBuilder& mb, float hl, float hw, float y0, float y1, u32 sides) {
    const float bow = hw * 1.7f;
    const float bw = hw * 0.75f; // narrower at the keel
    const glm::vec3 sl(-hl, y1, -hw), sr(-hl, y1, hw), ml(hl - bow, y1, -hw), mr(hl - bow, y1, hw), tip(hl, y1 + 0.02f, 0);
    const glm::vec3 slb(-hl + 0.02f, y0, -bw), srb(-hl + 0.02f, y0, bw), mlb(hl - bow, y0, -bw), mrb(hl - bow, y0, bw);
    const glm::vec3 tipb(hl - 0.05f, y0, 0);
    mb.quadFacing(slb, mlb, ml, sl, sides, {0, 0, -1});
    mb.quadFacing(srb, mrb, mr, sr, sides, {0, 0, 1});
    mb.quadFacing(mlb, tipb, tip, ml, sides, {1, 0, -1});
    mb.quadFacing(mrb, tipb, tip, mr, sides, {1, 0, 1});
    mb.quadFacing(slb, srb, sr, sl, sides, {-1, 0, 0});
    mb.quadFacing(sl, sr, mr, ml, kDeck, {0, 1, 0});
    mb.triangleUp(ml, mr, tip, kDeck);
    // Boot topping: a dark band just above the water line.
    mb.quadFacing({-hl + 0.012f, y0 + 0.035f, -hw * 0.9f - kInset}, {hl - bow, y0 + 0.035f, -hw * 0.9f - kInset},
                  {hl - bow, y0 + 0.055f, -hw * 0.93f - kInset}, {-hl + 0.012f, y0 + 0.055f, -hw * 0.93f - kInset},
                  kHullBottom, {0, 0, -1});
    mb.quadFacing({-hl + 0.012f, y0 + 0.035f, hw * 0.9f + kInset}, {hl - bow, y0 + 0.035f, hw * 0.9f + kInset},
                  {hl - bow, y0 + 0.055f, hw * 0.93f + kInset}, {-hl + 0.012f, y0 + 0.055f, hw * 0.93f + kInset},
                  kHullBottom, {0, 0, 1});
}

void harbourLaunch(MeshBuilder& mb) {
    const float hl = 0.36f, hw = 0.1f;
    hull(mb, hl, hw, -0.05f, 0.06f, kLivery);
    mb.box({-0.16f, 0.06f, -0.075f}, {0.1f, 0.15f, 0.075f}, kPaint);
    windowBand(mb, -0.14f, 0.08f, 0.09f, 0.13f, 0.075f);
    mb.box({-0.18f, 0.15f, -0.085f}, {0.12f, 0.165f, 0.085f}, kLiveryDark);
    mb.box({-0.3f, 0.06f, -0.09f}, {-0.29f, 0.12f, 0.09f}, kPaint); // stern rail
    mb.box({0.02f, 0.165f, -0.006f}, {0.032f, 0.28f, 0.006f}, rgba(60, 60, 64)); // mast
}

void coastalFerry(MeshBuilder& mb) {
    const float hl = 0.62f, hw = 0.17f;
    hull(mb, hl, hw, -0.06f, 0.07f, kLivery);
    // Two decks of cabins and a bridge.
    mb.box({-0.5f, 0.07f, -0.15f}, {0.3f, 0.18f, 0.15f}, kPaint);
    windowBand(mb, -0.47f, 0.27f, 0.11f, 0.155f, 0.15f);
    mb.box({-0.38f, 0.18f, -0.13f}, {0.2f, 0.27f, 0.13f}, kPaint);
    windowBand(mb, -0.35f, 0.17f, 0.21f, 0.25f, 0.13f);
    mb.box({0.06f, 0.27f, -0.11f}, {0.2f, 0.33f, 0.11f}, kPaint); // bridge
    mb.quadFacing({0.2f + kInset, 0.285f, -0.1f}, {0.2f + kInset, 0.285f, 0.1f}, {0.2f + kInset, 0.32f, 0.1f},
                  {0.2f + kInset, 0.32f, -0.1f}, kGlass, {1, 0, 0});
    mb.box({-0.4f, 0.27f, -0.14f}, {0.22f, 0.28f, 0.14f}, kRoofGrey);
    // Funnel in company colours with a black top.
    mb.prism({-0.18f, 0.28f, 0.0f}, 0.055f, 0.13f, 8, kLivery, true);
    mb.prism({-0.18f, 0.41f, 0.0f}, 0.056f, 0.03f, 8, rgba(30, 30, 32), true);
    // Lifeboats.
    for (const float z : {-0.145f, 0.145f})
        for (const float x : {-0.3f, -0.05f})
            mb.box({x - 0.06f, 0.2f, z - 0.02f}, {x + 0.06f, 0.24f, z + 0.02f}, rgba(236, 120, 40));
}

void hydrofoil(MeshBuilder& mb) {
    const float hl = 0.48f, hw = 0.12f;
    constexpr u32 kStrut = rgba(50, 52, 56);
    // Hull lifted clear of the water on foils.
    hull(mb, hl, hw, 0.04f, 0.13f, kPaint);
    for (const float x : {-0.3f, 0.28f}) {
        mb.box({x - 0.015f, -0.04f, -0.13f}, {x + 0.015f, 0.05f, -0.11f}, kStrut);
        mb.box({x - 0.015f, -0.04f, 0.11f}, {x + 0.015f, 0.05f, 0.13f}, kStrut);
        mb.box({x - 0.04f, -0.045f, -0.16f}, {x + 0.04f, -0.035f, 0.16f}, kStrut);
    }
    mb.box({-hl + 0.02f, 0.08f, -hw - kInset}, {hl - 0.2f, 0.1f, hw + kInset}, kLivery); // stripe
    mb.box({-0.36f, 0.13f, -0.1f}, {0.2f, 0.22f, 0.1f}, kPaint);
    mb.triangle({0.2f, 0.13f, -0.1f}, {0.2f, 0.22f, -0.1f}, {0.3f, 0.13f, -0.1f}, kPaint);
    mb.triangle({0.2f, 0.13f, 0.1f}, {0.3f, 0.13f, 0.1f}, {0.2f, 0.22f, 0.1f}, kPaint);
    mb.quadFacing({0.2f, 0.22f, -0.1f}, {0.3f, 0.13f, -0.1f}, {0.3f, 0.13f, 0.1f}, {0.2f, 0.22f, 0.1f}, kGlass,
                  {1, 1, 0});
    windowBand(mb, -0.33f, 0.17f, 0.155f, 0.2f, 0.1f);
    mb.box({-0.34f, 0.22f, -0.08f}, {0.16f, 0.23f, 0.08f}, kLiveryDark);
}

void dockPier(MeshBuilder& mb) {
    // Faces +Z (towards the water): a ticket hut on the shore and a timber pier over the berth.
    constexpr u32 kWood = rgba(132, 98, 66);
    constexpr u32 kPost = rgba(84, 64, 46);
    mb.box({-0.34f, 0.0f, -0.42f}, {0.02f, 0.2f, -0.12f}, kPaint);
    mb.box({-0.37f, 0.2f, -0.45f}, {0.05f, 0.23f, -0.09f}, kLivery);
    mb.quadFacing({-0.2f, 0.0f, -0.12f + kInset}, {-0.1f, 0.0f, -0.12f + kInset}, {-0.1f, 0.15f, -0.12f + kInset},
                  {-0.2f, 0.15f, -0.12f + kInset}, rgba(70, 56, 46), {0, 0, 1});
    // The pier ends just past the shore so moored ships fit in the berth beyond it.
    mb.box({-0.18f, -0.03f, -0.1f}, {0.22f, 0.02f, 0.62f}, kWood);
    for (const float z : {0.15f, 0.4f, 0.6f})
        for (const float x : {-0.16f, 0.2f}) mb.box({x - 0.02f, -0.9f, z - 0.02f}, {x + 0.02f, 0.0f, z + 0.02f}, kPost);
    for (const float z : {0.3f, 0.52f}) mb.box({0.17f, 0.02f, z - 0.015f}, {0.2f, 0.06f, z + 0.015f}, rgba(40, 40, 44));
    mb.box({-0.17f, 0.02f, 0.59f}, {0.21f, 0.06f, 0.61f}, kPaint); // end rail
    mb.box({0.3f, 0.0f, -0.3f}, {0.32f, 0.3f, -0.28f}, rgba(60, 60, 64)); // lamp post
    mb.box({0.28f, 0.3f, -0.32f}, {0.34f, 0.33f, -0.26f}, kLight);
}

void shipyard(MeshBuilder& mb) {
    // Faces +Z: an open-ended boat shed over a slipway running down into the water.
    constexpr u32 kShed = rgba(120, 128, 136);
    constexpr u32 kConcrete = rgba(160, 156, 148);
    const float x0 = -0.4f, x1 = 0.4f, z0 = -0.45f, z1 = 0.62f, h = 0.42f;
    mb.box({x0, 0.0f, z0}, {x0 + 0.04f, h, z1}, kShed);
    mb.box({x1 - 0.04f, 0.0f, z0}, {x1, h, z1}, kShed);
    mb.box({x0, 0.0f, z0}, {x1, h, z0 + 0.04f}, kShed);
    const glm::mat4 saved = mb.transform;
    mb.transform = saved * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    mb.gableRoofX({-z1, 0, x0}, {-z0, 0, x1}, h, 0.2f, kLiveryDark, kShed, 0.03f);
    mb.transform = saved;
    mb.box({x0 - 0.005f, h - 0.06f, z1 - 0.02f}, {x1 + 0.005f, h - 0.02f, z1 + 0.005f}, kLivery);
    // Slipway from the shed floor down below the water line.
    mb.quadFacing({x0 + 0.08f, 0.01f, z0 + 0.1f}, {x1 - 0.08f, 0.01f, z0 + 0.1f}, {x1 - 0.08f, 0.01f, 0.4f},
                  {x0 + 0.08f, 0.01f, 0.4f}, kConcrete, {0, 1, 0});
    mb.quadFacing({x0 + 0.08f, 0.01f, 0.4f}, {x1 - 0.08f, 0.01f, 0.4f}, {x1 - 0.08f, -0.6f, 1.05f},
                  {x0 + 0.08f, -0.6f, 1.05f}, kConcrete, {0, 1, 0.6f});
}

void smokeCloud(MeshBuilder& mb) {
    constexpr u32 kSmokeGrey = rgba(96, 96, 100);
    mb.icosphere({0.0f, 0.0f, 0.0f}, 0.09f, kSmokeGrey);
    mb.icosphere({0.07f, 0.08f, 0.03f}, 0.07f, rgba(120, 120, 124));
    mb.icosphere({-0.05f, 0.14f, -0.02f}, 0.06f, rgba(140, 140, 144));
}

void person(MeshBuilder& mb) {
    mb.box({-0.018f, 0.03f, -0.012f}, {0.018f, 0.07f, 0.012f}, tintable(1.0f));
    for (const float side : {-1.0f, 1.0f}) {
        mb.box({side * 0.01f - 0.006f, 0.0f, -0.009f}, {side * 0.01f + 0.006f, 0.035f, 0.009f}, rgba(50, 58, 76));
        mb.box({side * 0.025f - 0.005f, 0.035f, -0.008f}, {side * 0.025f + 0.005f, 0.065f, 0.008f}, rgba(226, 190, 160));
    }
    mb.box({-0.014f, 0.07f, -0.012f}, {0.014f, 0.098f, 0.012f}, rgba(226, 190, 160));
}

} // namespace

void buildTransportModels(MeshBuilder& out, ModelId id) {
    switch (id) {
    case models::kStationEntrance:
        out.box({-0.32f,0,-0.20f},{-0.12f,0.48f,0.20f},rgba(180,165,143));
        out.box({0.12f,0,-0.20f},{0.32f,0.48f,0.20f},rgba(180,165,143));
        out.box({-0.36f,0.48f,-0.24f},{0.36f,0.55f,0.24f},tintable(1.0f));
        out.box({-0.22f,0.36f,0.205f},{0.22f,0.45f,0.215f},rgba(50,95,160));
        out.box({-0.03f,0.37f,0.215f},{0.03f,0.44f,0.22f},rgba(240,240,235));
        break;
    case models::kFootbridge:
        out.box({-0.5f,-0.045f,-0.15f},{0.5f,0,0.15f},rgba(155,157,160));
        for(float z:{-0.15f,0.15f}) {
            out.box({-0.5f,0.15f,z-0.015f},{0.5f,0.18f,z+0.015f},rgba(68,78,88));
            for(float x:{-0.48f,0.0f,0.48f})out.box({x-0.015f,0,z-0.015f},{x+0.015f,0.18f,z+0.015f},rgba(68,78,88));
        }
        break;
    case models::kCrate:
        out.box({-0.035f, 0, -0.035f}, {0.035f, 0.07f, 0.035f}, tintable(1.0f));
        out.box({-0.037f, 0.025f, -0.037f}, {0.037f, 0.035f, 0.037f}, rgba(90, 65, 42));
        break;
    case models::kGrain:
        out.icosphere({0, 0.025f, 0}, 0.04f, rgba(220, 183, 87));
        break;
    case models::kFoodLogo:
        // White cross on green: refrigerated food freight.
        out.box({-0.06f, 0, -0.003f}, {0.06f, 0.1f, 0.003f}, rgba(48, 146, 80));
        out.box({-0.01f, 0.015f, -0.005f}, {0.01f, 0.085f, 0.005f}, kWhite);
        out.box({-0.04f, 0.04f, -0.005f}, {0.04f, 0.06f, 0.005f}, kWhite);
        break;
    case models::kGoodsLogo:
        out.box({-0.06f, 0, -0.003f}, {0.06f, 0.1f, 0.003f}, rgba(55, 107, 190));
        out.box({-0.03f, 0.02f, -0.005f}, {0.03f, 0.08f, 0.005f}, rgba(226, 180, 94));
        break;
    case models::bus(4): case models::bus(5): case models::bus(6): {
        wheels(out, -0.23f, 0.23f, 0.11f);
        out.box({-0.34f, 0.06f, -0.11f}, {0.34f, 0.1f, 0.11f}, kLiveryDark);
        out.box({0.17f, 0.1f, -0.11f}, {0.34f, 0.28f, 0.11f}, kLivery);
        windowBand(out, 0.19f, 0.30f, 0.19f, 0.25f, 0.11f);
        if (id != models::bus(4)) out.box({-0.33f, 0.1f, -0.105f}, {0.14f, 0.30f, 0.105f}, kWhite);
        else for (const float z : {-0.11f, 0.10f}) out.box({-0.33f, 0.1f, z}, {0.14f, 0.17f, z+0.01f}, kLivery);
        break;
    }
    case models::train(10): case models::train(11): case models::train(12): {
        wheels(out, -0.21f, 0.21f, 0.11f);
        out.box({-0.30f, 0.07f, -0.11f}, {0.30f, 0.12f, 0.11f}, kLiveryDark);
        if (id != models::train(10)) out.box({-0.29f, 0.12f, -0.11f}, {0.29f, 0.32f, 0.11f}, kWhite);
        else {
            for (const float z : {-0.11f, 0.10f}) out.box({-0.30f, 0.12f, z}, {0.30f, 0.22f, z+0.01f}, kLivery);
            for (const float x : {-0.30f, 0.29f}) out.box({x, 0.12f, -0.11f}, {x+0.01f, 0.22f, 0.11f}, kLivery);
        }
        break;
    }
    case models::ship(3): case models::ship(4): case models::ship(5):
        out.box({-0.67f, 0.02f, -0.23f}, {0.60f, 0.16f, 0.23f}, kLiveryDark);
        out.box({0.38f, 0.16f, -0.2f}, {0.59f, 0.42f, 0.2f}, kWhite);
        windowBand(out, 0.40f, 0.56f, 0.30f, 0.36f, 0.2f);
        if (id != models::ship(3)) out.box({-0.62f, 0.16f, -0.21f}, {0.30f, 0.32f, 0.21f}, kLivery);
        break;
    case models::bus(7): // timber lorry: cab and a flatbed with stanchions
        wheels(out, -0.23f, 0.23f, 0.11f);
        out.box({-0.34f, 0.06f, -0.11f}, {0.34f, 0.1f, 0.11f}, kLiveryDark);
        out.box({0.17f, 0.1f, -0.11f}, {0.34f, 0.28f, 0.11f}, kLivery);
        windowBand(out, 0.19f, 0.30f, 0.19f, 0.25f, 0.11f);
        for (const float x : {-0.32f, -0.1f, 0.12f})
            for (const float z : {-0.105f, 0.095f}) out.box({x, 0.1f, z}, {x + 0.015f, 0.22f, z + 0.01f}, kLiveryDark);
        break;
    case models::train(13): // flat wagon with stanchions
        wheels(out, -0.21f, 0.21f, 0.11f);
        out.box({-0.30f, 0.07f, -0.11f}, {0.30f, 0.12f, 0.11f}, kLiveryDark);
        for (const float x : {-0.29f, -0.1f, 0.1f, 0.28f})
            for (const float z : {-0.105f, 0.095f}) out.box({x, 0.12f, z}, {x + 0.015f, 0.24f, z + 0.01f}, kLivery);
        break;
    case models::ship(6): // deck cargo coaster: open well deck between bridge and forecastle
        out.box({-0.67f, 0.02f, -0.23f}, {0.60f, 0.16f, 0.23f}, kLiveryDark);
        out.box({0.38f, 0.16f, -0.2f}, {0.59f, 0.42f, 0.2f}, kWhite);
        windowBand(out, 0.40f, 0.56f, 0.30f, 0.36f, 0.2f);
        out.box({-0.67f, 0.16f, -0.21f}, {-0.55f, 0.26f, 0.21f}, kLivery);
        for (const float z : {-0.23f, 0.22f}) out.box({-0.55f, 0.16f, z}, {0.38f, 0.21f, z + 0.01f}, kLivery);
        break;
    case models::kStreetLamp: {
        constexpr u32 metal = rgba(64, 72, 78);
        out.prism({0, 0, 0}, 0.025f, 0.85f, 5, metal);
        out.box({-0.05f, 0.82f, -0.05f}, {0.05f, 0.95f, 0.05f}, rgba(232, 219, 164, 254));
        out.box({-0.07f, 0.95f, -0.07f}, {0.07f, 0.98f, 0.07f}, metal);
        out.box({-0.05f, 0.0f, -0.05f}, {0.05f, 0.06f, 0.05f}, metal);
        break;
    }
    case models::kBusStop: busStopShelter(out); break;
    case models::kRoadDepot: depotBuilding(out, false); break;
    case models::kTramDepot: depotBuilding(out, true); break;
    case models::kPerson: person(out); break;
    case models::kSteamWheelset: steamWheelset(out); break;
    case models::kSideRod: steamSideRod(out); break;
    case models::kMainRod: steamMainRod(out); break;
    case models::bus(0): busBody(out, 0.58f, 0.2f, true, false); break;
    case models::bus(1): busBody(out, 0.62f, 0.21f, false, false); break;
    case models::bus(2): busBody(out, 0.66f, 0.24f, false, false); break;
    case models::bus(3): busBody(out, 0.92f, 0.22f, false, true); break;
    case models::tram(0): tramBody(out, 0.85f, 1); break;
    case models::tram(1): tramBody(out, 1.1f, 2); break;
    case models::tram(2): tramBody(out, 1.35f, 3); break;
    case models::kRailDepot: railDepotBuilding(out); break;
    case models::kSignal: signalPost(out); break;
    case models::kPlatformCanopy: platformCanopy(out); break;
    case models::train(0): steamEngine(out); break;
    case models::train(1): dieselEngine(out, false); break;
    case models::train(2): dieselEngine(out, true); break;
    case models::train(3): railcar(out, 0.72f, true, false); break;
    case models::train(8): railcar(out, 0.6f, false, true); break;
    case models::train(9): railcar(out, 0.66f, false, false); break;
    case models::ship(0): harbourLaunch(out); break;
    case models::ship(1): coastalFerry(out); break;
    case models::ship(2): hydrofoil(out); break;
    case models::kDock: dockPier(out); break;
    case models::kShipDepot: shipyard(out); break;
    case models::kSmoke: smokeCloud(out); break;
    default: break;
    }
}

} // namespace tp::gfx
