#pragma once
// The complete, deterministic game state plus the per-tick simulation entry point.

#include "core/calendar.hpp"
#include "core/pool.hpp"
#include "core/rng.hpp"
#include "sim/company.hpp"
#include "sim/ids.hpp"
#include "sim/map.hpp"

#include <deque>
#include <optional>
#include <string>

namespace tp {

// Save game / snapshot format version.
inline constexpr u32 kSaveVersion = 1;

struct WorldSettings {
    u64 seed = 1;
    i32 startYear = 1950;
    Money initialLoan = 300'000;
    Money maxLoan = 1'000'000;
    i32 interestPercent = 4;
    i32 maxActiveChunks = 20'000;
    i32 maxChunksPerCommand = 64;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(seed, startYear, initialLoan, maxLoan, interestPercent, maxActiveChunks, maxChunksPerCommand);
    }
};

enum class NewsKind : u8 { General, Town, Company, Vehicle, Finance };

struct NewsItem {
    i64 tick = 0;
    NewsKind kind = NewsKind::General;
    std::string text;
    std::optional<TilePos> location;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(tick, kind, text, location);
    }
};

class World {
public:
    explicit World(const WorldSettings& settings = {});

    WorldSettings settings;
    GameClock clock;
    Rng rng;
    Map map;
    Pool<Company, CompanyId> companies;
    std::deque<NewsItem> news;

    // Advances the simulation by one tick (one in-game minute).
    void tick();

    // Activates a chunk and spawns its content. Returns false if it was already active.
    bool activateChunk(ChunkPos pos);

    void addNews(NewsKind kind, std::string text, std::optional<TilePos> where = std::nullopt);

    // Checksum over the game state, compared between peers to detect desyncs.
    u64 stateHash() const;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(settings);
        if constexpr (Ar::kReading) map = Map(settings.seed);
        ar(clock, rng, map, companies, news);
    }

private:
    void onNewDay();
    void onNewMonth();
    void onNewYear();
};

} // namespace tp
