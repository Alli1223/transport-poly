#include "render/models.hpp"

#include "sim/buildings.hpp"

#include <algorithm>
#include <cmath>

namespace tp::gfx {

ModelInstance makeInstance(const glm::vec3& pos, float yaw, float scale, u32 tint) {
    const float c = std::cos(yaw) * scale;
    const float s = std::sin(yaw) * scale;
    return ModelInstance{{c, 0.0f, s, pos.x}, {0.0f, scale, 0.0f, pos.y}, {-s, 0.0f, c, pos.z}, tint};
}

ModelInstance makeInstance(const glm::mat4& m, u32 tint) {
    return ModelInstance{{m[0][0], m[1][0], m[2][0], m[3][0]},
                         {m[0][1], m[1][1], m[2][1], m[3][1]},
                         {m[0][2], m[1][2], m[2][2], m[3][2]},
                         tint};
}

namespace {

constexpr u32 kTrunk = rgba(110, 78, 52);

void buildConifer(MeshBuilder& mb, int variant, bool snowy) {
    const u32 greens[3] = {rgba(46, 104, 58), rgba(56, 116, 62), rgba(40, 92, 64)};
    const u32 g = greens[variant % 3];
    const u32 top = snowy ? rgba(236, 242, 248) : shade(g, 1.08f);
    mb.prism({0, 0, 0}, 0.05f, 0.25f, 5, kTrunk, false);
    const float s = 1.0f + 0.1f * static_cast<float>(variant);
    mb.cone({0, 0.18f, 0}, 0.34f, 0.55f * s, 7, g, 0.3f);
    mb.cone({0, 0.46f * s, 0}, 0.26f, 0.48f * s, 7, shade(g, 1.05f), 0.8f);
    mb.cone({0, 0.74f * s, 0}, 0.17f, 0.4f * s, 6, top, 0.1f);
}

void buildDeciduous(MeshBuilder& mb, int variant) {
    const u32 greens[3] = {rgba(92, 152, 62), rgba(112, 164, 64), rgba(76, 136, 56)};
    const u32 g = greens[variant % 3];
    mb.prism({0, 0, 0}, 0.06f, 0.38f, 5, kTrunk, false);
    if (variant == 2) {
        mb.icosphere({0.08f, 0.62f, 0.02f}, 0.27f, g, 0, 0.85f);
        mb.icosphere({-0.1f, 0.52f, -0.06f}, 0.22f, shade(g, 0.93f), 0, 0.9f);
    } else {
        mb.icosphere({0, 0.62f, 0}, 0.32f + 0.03f * static_cast<float>(variant), g, 0, 0.9f);
    }
}

void buildPalm(MeshBuilder& mb, int variant) {
    // A leaning, segmented trunk under a crown of drooping fronds.
    const float lean = variant ? -0.06f : 0.07f;
    glm::vec3 at(0.0f);
    for (int i = 0; i < 4; ++i) {
        mb.frustum(at, 0.055f - i * 0.006f, 0.05f - i * 0.006f, 0.17f, 5, i % 2 ? rgba(128, 98, 66) : rgba(112, 86, 58));
        at += glm::vec3(lean * (0.4f + i * 0.25f), 0.17f, lean * 0.3f);
    }
    const u32 frond = variant ? rgba(64, 138, 58) : rgba(78, 150, 62);
    for (int k = 0; k < 7; ++k) {
        const float a = k * 6.2831853f / 7.0f + variant * 0.4f;
        const glm::vec3 dir(std::cos(a), 0.0f, std::sin(a));
        const glm::vec3 side(-dir.z, 0.0f, dir.x);
        const glm::vec3 mid = at + dir * 0.22f + glm::vec3(0, 0.05f, 0);
        const glm::vec3 tip = at + dir * 0.42f + glm::vec3(0, -0.12f, 0);
        mb.triangle(at, mid + side * 0.07f, mid - side * 0.07f, frond);
        mb.triangle(at, mid - side * 0.07f, mid + side * 0.07f, shade(frond, 0.85f));
        mb.triangle(mid - side * 0.07f, mid + side * 0.07f, tip, frond);
        mb.triangle(mid + side * 0.07f, mid - side * 0.07f, tip, shade(frond, 0.85f));
    }
    mb.icosphere(at + glm::vec3(0, -0.02f, 0), 0.05f, rgba(96, 72, 44));
}

void buildCactus(MeshBuilder& mb, int variant) {
    const u32 g = variant ? rgba(92, 140, 76) : rgba(80, 128, 70);
    const float h = variant ? 0.42f : 0.55f;
    mb.prism({0, 0, 0}, 0.06f, h, 6, g);
    mb.icosphere({0, h, 0}, 0.06f, g, 0, 0.7f);
    // Arms bending upwards.
    for (const float side : {-1.0f, 1.0f}) {
        if (variant && side > 0) continue;
        const float y = h * (side > 0 ? 0.45f : 0.3f);
        mb.box({side > 0 ? 0.0f : -0.15f, y - 0.03f, -0.03f}, {side > 0 ? 0.15f : 0.0f, y + 0.03f, 0.03f}, shade(g, 0.95f));
        mb.prism({side * 0.15f, y, 0}, 0.04f, h * 0.35f, 6, g);
        mb.icosphere({side * 0.15f, y + h * 0.35f, 0}, 0.04f, g, 0, 0.7f);
    }
}

void buildAcacia(MeshBuilder& mb, int variant) {
    // Thin forked trunk and a wide, flat crown.
    const u32 g = variant ? rgba(120, 146, 64) : rgba(104, 136, 58);
    mb.frustum({0, 0, 0}, 0.04f, 0.03f, 0.36f, 5, kTrunk);
    mb.frustum({0, 0.3f, 0}, 0.025f, 0.02f, 0.16f, 4, kTrunk);
    mb.icosphere({0.03f, 0.5f, 0.0f}, 0.38f + 0.05f * variant, g, 0, 0.22f);
    mb.icosphere({-0.12f, 0.46f, 0.08f}, 0.22f, shade(g, 0.92f), 0, 0.25f);
}

void buildShrub(MeshBuilder& mb, int variant) {
    const u32 g = variant ? rgba(132, 124, 78) : rgba(104, 112, 70);
    mb.icosphere({0, 0.08f, 0}, 0.14f, g, 0, 0.7f);
    mb.icosphere({0.1f, 0.06f, 0.05f}, 0.1f, shade(g, 0.9f), 0, 0.7f);
    mb.icosphere({-0.08f, 0.05f, -0.06f}, 0.09f, shade(g, 1.08f), 0, 0.7f);
}

void buildCloud(MeshBuilder& mb, int variant) {
    // A flat-bottomed heap of puffs, about one unit across.
    const u32 white = rgba(250, 251, 255);
    struct Puff {
        float x, y, z, r;
    };
    static constexpr Puff kPuffs[3][6] = {
        {{0, 0.1f, 0, 0.42f}, {0.38f, 0.05f, 0.08f, 0.3f}, {-0.36f, 0.04f, -0.05f, 0.32f}, {0.1f, 0.28f, 0.05f, 0.3f}, {-0.12f, 0.02f, 0.3f, 0.26f}, {0.2f, 0.02f, -0.3f, 0.24f}},
        {{0, 0.08f, 0, 0.36f}, {0.42f, 0.04f, 0, 0.34f}, {0.78f, 0.02f, 0.06f, 0.24f}, {-0.36f, 0.04f, 0.04f, 0.28f}, {0.22f, 0.24f, 0.02f, 0.26f}, {-0.05f, 0.02f, -0.26f, 0.22f}},
        {{0, 0.12f, 0, 0.46f}, {0.3f, 0.3f, 0.1f, 0.3f}, {-0.32f, 0.06f, 0.2f, 0.3f}, {0.4f, 0.04f, -0.22f, 0.28f}, {-0.4f, 0.02f, -0.25f, 0.26f}, {0.02f, 0.02f, 0.42f, 0.24f}},
    };
    for (const Puff& p : kPuffs[variant % 3]) mb.icosphere({p.x, p.y, p.z}, p.r, white, 1, 0.62f);
}

} // namespace

void ModelLibrary::build() {
    std::vector<Vertex> all;
    const auto add = [&](ModelId id, const MeshBuilder& mb) {
        for (const bool glass : {false, true}) {
            const auto begin = all.size();
            for (usize i = 0; i + 2 < mb.vertices.size(); i += 3) {
                if (((mb.vertices[i].color >> 24) == 253) != glass) continue;
                all.insert(all.end(), mb.vertices.begin() + i, mb.vertices.begin() + i + 3);
            }
            (glass ? glassRanges_ : ranges_)[id] = {static_cast<GLint>(begin),
                                                    static_cast<GLsizei>(all.size() - begin)};
        }
    };
    // Opaque vertices of trees take alpha 250, so the shaders sway them in the wind.
    const auto swaying = [](MeshBuilder& mb) {
        for (Vertex& vertex : mb.vertices)
            if ((vertex.color >> 24) == 255) vertex.color = (vertex.color & 0x00FFFFFFu) | (250u << 24);
        return mb;
    };
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildConifer(mb, v, false);
        add(static_cast<ModelId>(models::kTreeConifer0 + v), swaying(mb));
    }
    {
        MeshBuilder mb;
        buildConifer(mb, 1, true);
        add(models::kTreeConiferSnow, swaying(mb));
    }
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildDeciduous(mb, v);
        add(static_cast<ModelId>(models::kTreeDeciduous0 + v), swaying(mb));
    }
    for (int v = 0; v < 2; ++v) {
        const auto make = [&](ModelId base, void (*fn)(MeshBuilder&, int)) {
            MeshBuilder mb;
            fn(mb, v);
            add(static_cast<ModelId>(base + v), base == models::kTreeCactus0 ? mb : swaying(mb));
        };
        make(models::kTreePalm0, buildPalm);
        make(models::kTreeCactus0, buildCactus);
        make(models::kTreeAcacia0, buildAcacia);
        make(models::kShrub0, buildShrub);
    }
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildCloud(mb, v);
        add(static_cast<ModelId>(models::kCloud0 + v), mb);
    }
    for (const ModelId id :
         {models::kBusStop, models::kRoadDepot, models::kTramDepot, models::kPerson, models::kRailDepot,
          models::kSignal, models::kPlatformCanopy, models::kDock, models::kShipDepot, models::kSmoke,
          models::kStreetLamp, models::kCrate, models::kGrain, models::kFoodLogo, models::kGoodsLogo,
          models::kStationEntrance, models::kFootbridge, models::kSeatedPerson, models::kStationApproach}) {
        MeshBuilder mb;
        buildTransportModels(mb, id);
        add(id, mb);
    }
    for (int v = 0; v < 8; ++v) {
        for (const ModelId id :
             {models::bus(v), models::tram(v), models::train(v), models::train(v + 8), models::ship(v)}) {
            MeshBuilder mb;
            buildTransportModels(mb, id);
            if (!mb.vertices.empty()) add(id, mb);
        }
    }
    for (ModelId id = models::kTramSectionBase; id <= models::kTramBellows; ++id) {
        MeshBuilder mb;
        buildTransportModels(mb, id);
        if (!mb.vertices.empty()) add(id, mb);
    }
    for (const ModelId id : {models::kSteamWheelset, models::kSideRod, models::kMainRod, models::kCatenaryMast,
                             models::kCatenaryWire, models::kPantograph}) {
        MeshBuilder mb;
        buildTransportModels(mb, id);
        add(id, mb);
    }
    const auto types = buildingTypes();
    for (u16 t = 0; t < types.size() && t < models::kMaxBuildingTypes; ++t) {
        for (u8 v = 0; v < models::kBuildingVariants; ++v) {
            MeshBuilder mb;
            buildBuildingModel(mb, t, t >= 15 && t <= 28 ? v : static_cast<u8>(v % std::max<u8>(1, types[t].variants)));
            add(models::building(t, v), mb);
        }
    }

    vao_ = makeVertexArray();
    vbo_ = makeBuffer();
    glBindVertexArray(vao_.get());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(all.size() * sizeof(Vertex)), all.data(), GL_STATIC_DRAW);
    setupVertexAttributes();
    for (GLuint loc = 3; loc <= 6; ++loc) {
        glEnableVertexAttribArray(loc);
        glVertexAttribDivisor(loc, 1);
    }
    glBindVertexArray(0);
}

void ModelLibrary::drawInstanced(ModelId id, GLuint instanceBuffer, usize byteOffset, GLsizei count,
                                 bool glass) const {
    const Range r = glass ? glassRanges_[id] : range(id);
    if (count <= 0 || r.count == 0) return;
    glBindVertexArray(vao_.get());
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
    constexpr GLsizei stride = sizeof(ModelInstance);
    const auto at = [&](usize off) { return reinterpret_cast<const void*>(byteOffset + off); };
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, at(0));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, at(16));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, at(32));
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(48));
    glDrawArraysInstanced(GL_TRIANGLES, r.first, r.count, count);
}

void InstanceBatch::clear() {
    for (auto& v : perModel_) v.clear();
}

usize InstanceBatch::size() const {
    usize n = 0;
    for (const auto& v : perModel_) n += v.size();
    return n;
}

void InstanceBatch::upload() {
    staging_.clear();
    for (usize i = 0; i < kModelCount; ++i) {
        offsets_[i] = staging_.size();
        staging_.insert(staging_.end(), perModel_[i].begin(), perModel_[i].end());
    }
    if (staging_.empty()) return;
    if (!buffer_) buffer_ = makeBuffer();
    glBindBuffer(GL_ARRAY_BUFFER, buffer_.get());
    const usize bytes = staging_.size() * sizeof(ModelInstance);
    if (bytes > capacity_) capacity_ = bytes * 2;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_), nullptr, GL_STREAM_DRAW); // orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), staging_.data());
}

void InstanceBatch::draw(const ModelLibrary& lib, bool glass) const {
    if (staging_.empty()) return;
    for (usize i = 0; i < kModelCount; ++i) {
        if (perModel_[i].empty()) continue;
        lib.drawInstanced(static_cast<ModelId>(i), buffer_.get(), offsets_[i] * sizeof(ModelInstance),
                          static_cast<GLsizei>(perModel_[i].size()), glass);
    }
}

void StaticInstances::upload(std::vector<std::pair<ModelId, ModelInstance>>& items) {
    batches.clear();
    total = items.size();
    if (items.empty()) return;
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<ModelInstance> data;
    data.reserve(items.size());
    for (usize i = 0; i < items.size(); ++i) {
        if (batches.empty() || batches.back().model != items[i].first)
            batches.push_back({items[i].first, static_cast<u32>(i), 0});
        ++batches.back().count;
        data.push_back(items[i].second);
    }
    if (!buffer) buffer = makeBuffer();
    glBindBuffer(GL_ARRAY_BUFFER, buffer.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(ModelInstance)), data.data(),
                 GL_STATIC_DRAW);
}

void StaticInstances::draw(const ModelLibrary& lib) const {
    for (const Batch& b : batches)
        lib.drawInstanced(b.model, buffer.get(), b.offset * sizeof(ModelInstance), static_cast<GLsizei>(b.count));
}

} // namespace tp::gfx
