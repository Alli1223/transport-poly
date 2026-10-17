#include "sim/map.hpp"

namespace tp {

Chunk* Map::chunk(ChunkPos p) {
    const auto it = chunks_.find(p.key());
    return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk* Map::chunk(ChunkPos p) const {
    const auto it = chunks_.find(p.key());
    return it == chunks_.end() ? nullptr : it->second.get();
}

bool Map::activate(ChunkPos p) {
    if (isActive(p)) return false;
    auto c = std::make_unique<Chunk>();
    gen_.generateChunk(p, *c);
    chunks_[p.key()] = std::move(c);
    order_.push_back(p);
    return true;
}

Tile* Map::tile(TilePos t) {
    Chunk* c = chunk(chunkOf(t));
    return c ? &c->tile(localCoord(t.x), localCoord(t.y)) : nullptr;
}

const Tile* Map::tile(TilePos t) const {
    const Chunk* c = chunk(chunkOf(t));
    return c ? &c->tile(localCoord(t.x), localCoord(t.y)) : nullptr;
}

u8 Map::cornerHeight(i32 cx, i32 cy) const {
    if (const Chunk* c = chunk(chunkOf({cx, cy}))) return c->height(localCoord(cx), localCoord(cy));
    return gen_.cornerHeight(cx, cy);
}

TileCorners Map::corners(TilePos t) const {
    if (const Chunk* c = chunk(chunkOf(t))) return c->corners(localCoord(t.x), localCoord(t.y));
    return TileCorners{{gen_.cornerHeight(t.x, t.y), gen_.cornerHeight(t.x + 1, t.y),
                        gen_.cornerHeight(t.x + 1, t.y + 1), gen_.cornerHeight(t.x, t.y + 1)}};
}

namespace {
// Calls fn(chunkPos, localX, localY) for every chunk copy of corner (cx, cy).
template <class Fn>
void forEachCornerCopy(i32 cx, i32 cy, Fn&& fn) {
    const ChunkPos primary = chunkOf({cx, cy});
    const i32 lx = localCoord(cx);
    const i32 ly = localCoord(cy);
    fn(primary, lx, ly);
    if (lx == 0) fn(ChunkPos{primary.x - 1, primary.y}, kChunkSize, ly);
    if (ly == 0) fn(ChunkPos{primary.x, primary.y - 1}, lx, kChunkSize);
    if (lx == 0 && ly == 0) fn(ChunkPos{primary.x - 1, primary.y - 1}, kChunkSize, kChunkSize);
}
} // namespace

bool Map::cornerEditable(i32 cx, i32 cy) const {
    bool ok = true;
    forEachCornerCopy(cx, cy, [&](ChunkPos p, i32, i32) { ok = ok && isActive(p); });
    return ok;
}

void Map::setCornerHeight(i32 cx, i32 cy, u8 height) {
    TP_ASSERT(cornerEditable(cx, cy));
    forEachCornerCopy(cx, cy, [&](ChunkPos p, i32 lx, i32 ly) {
        Chunk* c = chunk(p);
        c->height(lx, ly) = height;
        ++c->revision;
    });
}

void Map::touch(TilePos t) {
    if (Chunk* c = chunk(chunkOf(t))) ++c->revision;
}

} // namespace tp
