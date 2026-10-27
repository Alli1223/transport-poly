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

u8 Map::cornerHeight(i32 cx, i32 cy) const { return levelOf(cornerFine(cx, cy)); }

u16 Map::cornerFine(i32 cx, i32 cy) const {
    if (const Chunk* c = chunk(chunkOf({cx, cy}))) return c->fine(localCoord(cx), localCoord(cy));
    return gen_.cornerFine(cx, cy);
}

TileCorners Map::corners(TilePos t) const {
    if (const Chunk* c = chunk(chunkOf(t))) return c->corners(localCoord(t.x), localCoord(t.y));
    return TileCorners{{gen_.cornerHeight(t.x, t.y), gen_.cornerHeight(t.x + 1, t.y),
                        gen_.cornerHeight(t.x + 1, t.y + 1), gen_.cornerHeight(t.x, t.y + 1)}};
}

FineCorners Map::fineCorners(TilePos t) const {
    if (const Chunk* c = chunk(chunkOf(t))) return c->fineCorners(localCoord(t.x), localCoord(t.y));
    return FineCorners{{gen_.cornerFine(t.x, t.y), gen_.cornerFine(t.x + 1, t.y), gen_.cornerFine(t.x + 1, t.y + 1),
                        gen_.cornerFine(t.x, t.y + 1)}};
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

void Map::setCornerFine(i32 cx, i32 cy, u16 fine) {
    TP_ASSERT(cornerEditable(cx, cy));
    forEachCornerCopy(cx, cy, [&](ChunkPos p, i32 lx, i32 ly) {
        Chunk* c = chunk(p);
        c->fine(lx, ly) = fine;
        ++c->revision;
    });
}

void Map::touch(TilePos t) {
    const ChunkPos at = chunkOf(t);
    Chunk* c = chunk(at);
    if (!c) return;
    ++c->revision;
    // Ground colours blend across chunk borders: a tile on the edge redraws the chunks beside it.
    const i32 lx = localCoord(t.x), ly = localCoord(t.y);
    const i32 dx = lx == 0 ? -1 : lx == kChunkSize - 1 ? 1 : 0, dy = ly == 0 ? -1 : ly == kChunkSize - 1 ? 1 : 0;
    for (const ChunkPos n : {ChunkPos{at.x + dx, at.y}, ChunkPos{at.x, at.y + dy}, ChunkPos{at.x + dx, at.y + dy}})
        if (Chunk* beside = n == at ? nullptr : chunk(n)) ++beside->revision;
    const Tile& tile = c->tile(lx, ly);
    // Free-form track beds follow their own smooth profile and are not snapped.
    if (tile.type == TileType::Clear || tile.type == TileType::Water || tile.type == TileType::Track) return;
    if (tile.type == TileType::Depot && (tile.extra2 & kFreeTrackDepot)) return;
    snapToLevels(t);
}

void Map::snapToLevels(TilePos t) {
    const TilePos corners[4] = {{t.x, t.y}, {t.x + 1, t.y}, {t.x + 1, t.y + 1}, {t.x, t.y + 1}};
    for (const TilePos k : corners) {
        const u16 f = cornerFine(k.x, k.y);
        const u16 snapped = fineOf(levelOf(f));
        // Corners shared with unexplored chunks stay as generated.
        if (snapped != f && cornerEditable(k.x, k.y)) setCornerFine(k.x, k.y, snapped);
    }
}

} // namespace tp
