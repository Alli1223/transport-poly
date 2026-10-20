#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <map>

namespace tp {
namespace {

gfx::ModelId modelId(const VehicleModel& m) {
    switch (m.type) {
    case VehicleType::Road: return gfx::models::bus(m.visual);
    case VehicleType::Tram: return gfx::models::tram(m.visual);
    case VehicleType::Rail: return gfx::models::train(m.visual);
    case VehicleType::Ship: return gfx::models::ship(m.visual);
    }
    return gfx::models::bus(0);
}

// Project the actual procedural mesh, including the livery, roof and wagon-specific details.
// The small catalogue images need no framebuffer or extra OpenGL state during UI drawing.
void drawModelPreview(const VehicleModel& m, ImVec2 at, ImVec2 size, u32 livery) {
    static std::map<gfx::ModelId, std::vector<gfx::Vertex>> cache;
    const auto id = modelId(m);
    if (!cache.contains(id)) {
        gfx::MeshBuilder mesh;
        gfx::buildTransportModels(mesh, id);
        cache[id] = std::move(mesh.vertices);
    }
    const auto& vertices = cache[id];
    if (vertices.empty()) return;
    const glm::mat4 view = glm::lookAt(glm::vec3(1.2f, 0.9f, 2.5f), glm::vec3(0, 0.12f, 0), glm::vec3(0, 1, 0));
    std::vector<glm::vec3> projected;
    glm::vec2 lo(FLT_MAX), hi(-FLT_MAX);
    for (const auto& v : vertices) {
        glm::vec3 p = v.pos;
        if (m.type == VehicleType::Rail && m.capacity) p.x *= m.length / 12.0f;
        const auto q = glm::vec3(view * glm::vec4(p, 1));
        projected.push_back(q);
        lo = glm::min(lo, glm::vec2(q)); hi = glm::max(hi, glm::vec2(q));
    }
    const float scale = std::min((size.x - 12) / std::max(hi.x - lo.x, 0.01f),
                                 (size.y - 12) / std::max(hi.y - lo.y, 0.01f));
    const auto point = [&](usize i) {
        const auto p = projected[i];
        return ImVec2(at.x + size.x / 2 + (p.x - (lo.x + hi.x) / 2) * scale,
                      at.y + size.y / 2 - (p.y - (lo.y + hi.y) / 2) * scale);
    };
    std::vector<usize> triangles;
    for (usize i = 0; i + 2 < vertices.size(); i += 3) triangles.push_back(i);
    std::stable_sort(triangles.begin(), triangles.end(), [&](usize a, usize b) {
        return projected[a].z + projected[a+1].z + projected[a+2].z < projected[b].z + projected[b+1].z + projected[b+2].z;
    });
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
    for (const auto i : triangles) {
        const auto a = vertices[i], b = vertices[i+1], c = vertices[i+2];
        glm::vec3 color = gfx::unpackRgb(a.color);
        if ((a.color >> 24) == 0) color = gfx::unpackRgb(livery) * (color.r * 2.0f);
        const auto normal = glm::cross(b.pos - a.pos, c.pos - a.pos);
        const float brightness = glm::length(normal) > 0 ? 0.60f + 0.40f * std::max(0.0f, glm::dot(glm::normalize(normal), glm::normalize(glm::vec3(0.3f, 1, 0.6f)))) : 1;
        color = glm::clamp(color * brightness, glm::vec3(0), glm::vec3(1));
        const auto tint = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, (a.color >> 24) == 253 ? 0.35f : 1.0f));
        draw->AddTriangleFilled(point(i), point(i+1), point(i+2), tint);
    }
    draw->PopClipRect();
}
} // namespace

void App::drawVehicleCatalogue(DepotId depotId) {
    World& w = world();
    const auto* depot = w.depots.tryGet(depotId);
    if (!depot) return;
    const bool rail = depot->kind == DepotKind::Rail;
    const VehicleType type = rail ? VehicleType::Rail : depot->kind == DepotKind::Tram ? VehicleType::Tram
        : depot->kind == DepotKind::Ship ? VehicleType::Ship : VehicleType::Road;
    const auto usable = [&](VehicleId id) {
        const auto* v = w.vehicles.tryGet(id);
        return v && v->owner == session_->localCompany() && v->depot == depotId &&
            v->state == VehicleState::InDepot && v->stopped;
    };
    if (!usable(consistVehicle_)) {
        consistVehicle_ = {};
        if (rail) for (const auto [id, v] : w.vehicles) if (usable(id)) { consistVehicle_ = id; break; }
    }
    const float s = uiScale_;
    const auto* company = w.companies.tryGet(depot->owner);
    const u32 livery = ui::rgb(companyColourRgb(company ? company->colour : 0));
    ui::sectionHeader(rail ? "Train builder" : "Vehicle catalogue");
    if (rail && requestedCatalogueTab_ >= 0) ImGui::SetScrollHereY(0.15f);
    if (rail) {
        if (usable(consistVehicle_))
            ImGui::Text("Adding to Train %u - select its consist above to change train", w.vehicles[consistVehicle_].unitNumber);
        else ui::mutedText("Buy an engine, then add carriages with one click.");
    }
    const auto list = [&](int category) {
        ImGui::PushID(category);
        ImGui::BeginChild("##catalogue", ImVec2(0, 268 * s), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const auto models = vehicleModels();
        for (u16 i = 0; i < models.size(); ++i) {
            const auto& m = models[i];
            if (m.type != type || m.introYear > w.clock.date().year) continue;
            if (rail && ((category == 0 && !m.powered) || (category == 1 && (m.powered || !m.capacity)) ||
                         (category == 2 && (m.powered || !m.cargoCapacity)))) continue;
            ImGui::PushID(i);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x, height = 88 * s;
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), ui::col::kCard, 8 * s);
            drawModelPreview(m, ImVec2(at.x + 4*s, at.y + 5*s), ImVec2(112*s, 76*s), livery);
            const float textX = at.x + 124 * s;
            draw->PushClipRect(ImVec2(textX, at.y), ImVec2(at.x + width - 124*s, at.y + height), true);
            draw->AddText(ImVec2(textX, at.y + 10*s), ui::col::kText, m.name.data());
            const std::string specs = m.capacity ? std::format("{} seats  |  {} km/h", m.capacity, m.maxSpeedKmh)
                : m.cargoCapacity ? std::format("{} {}  |  {} km/h", m.cargoCapacity, cargoName(m.cargoType), m.maxSpeedKmh)
                : std::format("{} km/h  |  locomotive", m.maxSpeedKmh);
            auto* font = ui::fonts().small;
            draw->AddText(font, font->FontSize, ImVec2(textX, at.y + 35*s), ui::col::kTextMuted, specs.c_str());
            const auto price = std::format("{}  |  {}/yr", formatMoney(w.inflated(m.price)), formatMoney(w.inflated(m.runningCost)));
            draw->AddText(font, font->FontSize, ImVec2(textX, at.y + 59*s), ui::col::kTextMuted, price.c_str());
            draw->PopClipRect();
            ImGui::Dummy(ImVec2(width, height));
            const auto buy = [&](const char* label, CommandPayload payload, float y) {
                Command command; command.company = session_->localCompany(); command.payload = payload;
                const auto quote = testCommand(w, command);
                ImGui::SetCursorScreenPos(ImVec2(at.x + width - 116*s, at.y + y*s));
                ImGui::BeginDisabled(!quote.ok);
                if (ImGui::Button(label, ImVec2(108*s, 28*s))) {
                    const auto seq = session_->submit(payload);
                    if (std::holds_alternative<CmdBuyVehicle>(payload)) pendingBuySeq_ = seq;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", quote.ok ? std::format("{} | Introduced {} | {} year life", formatMoney(quote.cost), m.introYear, m.lifeYears).c_str() : quote.error.c_str());
            };
            if (!rail || category == 0) {
                buy(rail ? "Buy engine" : "Buy vehicle", CmdBuyVehicle{depotId, i}, rail && usable(consistVehicle_) ? 12.0f : 30.0f);
                if (rail && usable(consistVehicle_)) buy("Add engine", CmdAddCarriage{consistVehicle_, i}, 48.0f);
            } else buy(category == 1 ? "Buy carriage" : "Buy wagon", CmdAddCarriage{consistVehicle_, i}, 30.0f);
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + height + 6*s));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopID();
    };
    if (rail) {
        if (ImGui::BeginTabBar("##trainCatalogue")) {
            constexpr std::array<const char*, 3> tabs{"Engines", "Passenger carriages", "Freight wagons"};
            for (int i = 0; i < 3; ++i) {
                if (ImGui::BeginTabItem(tabs[i], nullptr, requestedCatalogueTab_ == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                    list(i); ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
            requestedCatalogueTab_ = -1;
        }
    } else list(0);
}
} // namespace tp
