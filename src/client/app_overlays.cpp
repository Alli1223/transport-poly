#include "client/app.hpp"
#include "core/math.hpp"
#include "render/picking.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace tp {
void App::drawWorldOverlays(std::vector<gfx::OverlayQuad>& overlays) {
    if (!overlayMode_) return;
    const World& w = world();
    const glm::dvec2 camera(camera_.target.x, camera_.target.z);
    if (overlayMode_ == 1) {
        for (const auto [id, s] : w.stations) {
            const glm::dvec2 at(s.sign.x, s.sign.y);
            if (glm::length(at - camera) > 150.0) continue;
            addCatchmentOverlay(overlays, s.tiles, s.catchment, gfx::rgba(90, 170, 255, 55));
            if (overlays.size() >= 8192) { overlays.resize(8192); break; }
        }
    } else if (overlayMode_ == 3) {
        std::vector<const Town*> visibleTowns;
        for (const auto [id, t] : w.towns)
            if (glm::length(glm::dvec2(t.centre.x, t.centre.y) - camera) < 90.0 + t.radius) visibleTowns.push_back(&t);
        const i32 cx = static_cast<i32>(std::floor(camera.x)), cy = static_cast<i32>(std::floor(camera.y));
        for (i32 y = cy - 40; y <= cy + 40; ++y)
            for (i32 x = cx - 40; x <= cx + 40; ++x) {
                if (!w.map.tile({x, y})) continue;
                const Town* nearest = nullptr;
                i64 best = 0;
                for (const Town* t : visibleTowns) {
                    const i64 distance = chebyshev(x - t->centre.x, y - t->centre.y);
                    if (distance <= t->radius + 3 && (!nearest || distance < best)) { nearest = t; best = distance; }
                }
                if (!nearest) continue;
                const u8 zone = towns::zoneAt(*nearest, {x, y});
                const u32 colour = zone == kZoneCore ? gfx::rgba(190, 100, 245, 90) :
                                   zone == kZoneInner ? gfx::rgba(90, 170, 255, 75) : gfx::rgba(100, 210, 100, 55);
                addTileOverlay(overlays, {x, y}, colour, 0.08);
            }
    } else if (overlayMode_ == 2) {
        std::map<std::pair<StationId, StationId>, usize> flow;
        for (const auto [id, s] : w.stations)
            for (const auto& [next, queue] : s.waiting) flow[{id, next}] += queue.size();
        for (const auto [id, vehicle] : w.vehicles)
            for (const PersonId pid : vehicle.passengers)
                if (const Person* p = w.people.tryGet(pid)) ++flow[{p->station, p->targetStation}];
        usize links = 0;
        for (const auto& [pair, count] : flow) {
            if (!count || pair.first == pair.second) continue;
            const Station* from = w.stations.tryGet(pair.first);
            const Station* to = w.stations.tryGet(pair.second);
            if (!from || !to) continue;
            const glm::dvec2 a(from->sign.x + 0.5, from->sign.y + 0.5);
            const glm::dvec2 b(to->sign.x + 0.5, to->sign.y + 0.5);
            if (glm::length(a - camera) > 180.0 && glm::length(b - camera) > 180.0) continue;
            const glm::dvec2 delta = b - a;
            const double length = glm::length(delta);
            if (length < 0.01) continue;
            const glm::dvec2 side(-delta.y / length, delta.x / length);
            const double width = 0.04 + std::min<usize>(count, 200) * 0.0015;
            const auto point = [&](glm::dvec2 p) { return glm::dvec3(p.x, gfx::terrainHeightAt(w.map, p.x, p.y) + 0.13, p.y); };
            for (int i = 0; i < 16; ++i) {
                const glm::dvec2 p = a + delta * (i / 16.0), q = a + delta * ((i + 1) / 16.0);
                overlays.push_back({{point(p - side * width), point(p + side * width),
                                     point(q + side * width), point(q - side * width)}, gfx::rgba(55, 210, 220, 140)});
            }
            const glm::dvec2 tail = a + delta * 0.72, tip = a + delta * 0.87;
            overlays.push_back({{point(tail - side * (width + 0.16)), point(tail + side * (width + 0.16)),
                                 point(tip + side * 0.005), point(tip - side * 0.005)}, gfx::rgba(255, 198, 70, 210)});
            if (++links >= 128) break;
        }
    }
}
}
