// Procedural low poly models for vehicles, stops and depots. Vehicles face +X; tintable parts
// take the owning company's colour.

#include "render/models.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace tp::gfx {

namespace {

const u32 kLivery = tintable(1.0f);
const u32 kLiveryDark = tintable(0.75f);
constexpr u32 kWhite = rgba(236, 236, 230);
constexpr u32 kGlass = rgba(58, 78, 100);
constexpr u32 kTyre = rgba(34, 34, 38);
constexpr u32 kChrome = rgba(170, 172, 176);
constexpr u32 kRoofGrey = rgba(120, 124, 130);
constexpr u32 kLight = rgba(250, 236, 170);
constexpr float kInset = 0.004f;

void wheels(MeshBuilder& mb, float x0, float x1, float halfW, float r = 0.045f) {
    for (const float x : {x0, x1}) {
        mb.box({x - r, 0.0f, halfW - 0.035f}, {x + r, r * 2.0f, halfW + 0.005f}, kTyre);
        mb.box({x - r, 0.0f, -halfW - 0.005f}, {x + r, r * 2.0f, -halfW + 0.035f}, kTyre);
    }
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

void person(MeshBuilder& mb) {
    mb.box({-0.018f, 0.0f, -0.012f}, {0.018f, 0.07f, 0.012f}, tintable(1.0f));
    mb.box({-0.014f, 0.07f, -0.012f}, {0.014f, 0.098f, 0.012f}, rgba(226, 190, 160));
}

} // namespace

void buildTransportModels(MeshBuilder& out, ModelId id) {
    switch (id) {
    case models::kBusStop: busStopShelter(out); break;
    case models::kRoadDepot: depotBuilding(out, false); break;
    case models::kTramDepot: depotBuilding(out, true); break;
    case models::kPerson: person(out); break;
    case models::bus(0): busBody(out, 0.58f, 0.2f, true, false); break;
    case models::bus(1): busBody(out, 0.62f, 0.21f, false, false); break;
    case models::bus(2): busBody(out, 0.66f, 0.24f, false, false); break;
    case models::bus(3): busBody(out, 0.92f, 0.22f, false, true); break;
    case models::tram(0): tramBody(out, 0.85f, 1); break;
    case models::tram(1): tramBody(out, 1.1f, 2); break;
    case models::tram(2): tramBody(out, 1.35f, 3); break;
    default: break;
    }
}

} // namespace tp::gfx
