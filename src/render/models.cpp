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

} // namespace

void ModelLibrary::build() {
    std::vector<Vertex> all;
    const auto add = [&](ModelId id, const MeshBuilder& mb) {
        ranges_[id] = {static_cast<GLint>(all.size()), static_cast<GLsizei>(mb.vertices.size())};
        all.insert(all.end(), mb.vertices.begin(), mb.vertices.end());
    };
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildConifer(mb, v, false);
        add(static_cast<ModelId>(models::kTreeConifer0 + v), mb);
    }
    {
        MeshBuilder mb;
        buildConifer(mb, 1, true);
        add(models::kTreeConiferSnow, mb);
    }
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildDeciduous(mb, v);
        add(static_cast<ModelId>(models::kTreeDeciduous0 + v), mb);
    }
    for (const ModelId id : {models::kBusStop, models::kRoadDepot, models::kTramDepot, models::kPerson,
                             models::kRailDepot, models::kSignal, models::kPlatformCanopy, models::kDock,
                             models::kShipDepot, models::kSmoke, models::kStreetLamp}) {
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
    const auto types = buildingTypes();
    for (u16 t = 0; t < types.size() && t < models::kMaxBuildingTypes; ++t) {
        for (u8 v = 0; v < models::kBuildingVariants; ++v) {
            MeshBuilder mb;
            buildBuildingModel(mb, t, static_cast<u8>(v % std::max<u8>(1, types[t].variants)));
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

void ModelLibrary::drawInstanced(ModelId id, GLuint instanceBuffer, usize byteOffset, GLsizei count) const {
    const Range r = range(id);
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

void InstanceBatch::draw(const ModelLibrary& lib) const {
    if (staging_.empty()) return;
    for (usize i = 0; i < kModelCount; ++i) {
        if (perModel_[i].empty()) continue;
        lib.drawInstanced(static_cast<ModelId>(i), buffer_.get(), offsets_[i] * sizeof(ModelInstance),
                          static_cast<GLsizei>(perModel_[i].size()));
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
