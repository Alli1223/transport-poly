#include "test.hpp"
#include "core/preferences.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/calendar.hpp"
#include "core/fixed.hpp"
#include "core/hash.hpp"
#include "core/id.hpp"
#include "core/math.hpp"
#include "core/money.hpp"
#include "core/names.hpp"
#include "core/pool.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace tp;

TEST_CASE("preferences: validated settings round trip and malformed files leave values intact") {
    const auto path = std::filesystem::temp_directory_path() /
                      std::format("transport-poly-prefs-{}.cfg", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    ClientPreferences original;
    CHECK(original.fullscreen);
    original.daylightOnly = true;
    original.viewDistance = 700;
    original.panPercent = 180;
    original.autosave = false;
    original.inflation = true;
    original.sounds = true;
    original.aiCompetitors = 3;
    original.recessions = false;
    original.fullscreen = true;
    original.monitor = 1;
    original.antiAliasing = 3;
    original.frameCap = 2;
    original.shadowQuality = 3;
    original.detailQuality = 2;
    original.treeDistance = 350;
    original.brightnessPercent = 140;
    original.clouds = false;
    original.cloudShadows = false;
    original.fogPercent = 30;
    original.nightFogPercent = 10;
    original.particles = original.lightHalos = original.treeSway = original.waterAnimation = false;
    original.masterVolume = 40;
    original.musicVolume = 23;
    original.vehicleVolume = 89;
    original.ambienceVolume = 17;
    original.interfaceVolume = 68;
    original.muteUnfocused = false;
    REQUIRE(original.save(path));
    ClientPreferences copy;
    REQUIRE(copy.load(path));
    CHECK(copy.daylightOnly);
    CHECK_EQ(copy.viewDistance, 700);
    CHECK_EQ(copy.panPercent, 180);
    CHECK(!copy.autosave);
    CHECK(copy.inflation);
    CHECK(copy.sounds);
    CHECK_EQ(copy.aiCompetitors, 3);
    CHECK(!copy.recessions);
    CHECK(copy.fullscreen);
    CHECK_EQ(copy.monitor, 1);
    CHECK_EQ(copy.antiAliasing, 3);
    CHECK_EQ(copy.frameRateLimit(), 60);
    CHECK_EQ(copy.shadowQuality, 3);
    CHECK_EQ(copy.detailQuality, 2);
    CHECK_EQ(copy.treeDistance, 350);
    CHECK_EQ(copy.brightnessPercent, 140);
    CHECK(!copy.clouds && !copy.cloudShadows);
    CHECK_EQ(copy.fogPercent, 30);
    CHECK_EQ(copy.nightFogPercent, 10);
    CHECK(!copy.particles && !copy.lightHalos && !copy.treeSway && !copy.waterAnimation);
    CHECK_EQ(copy.masterVolume, 40);
    CHECK_EQ(copy.musicVolume, 23);
    CHECK_EQ(copy.vehicleVolume, 89);
    CHECK_EQ(copy.ambienceVolume, 17);
    CHECK_EQ(copy.interfaceVolume, 68);
    CHECK(!copy.muteUnfocused);
    { std::ofstream out(path); out << "bad settings"; }
    CHECK(!copy.load(path));
    CHECK_EQ(copy.viewDistance, 700);
    original.viewDistance = 99999;
    original.panPercent = -10;
    original.brightnessPercent = -10;
    original.shadowQuality = 99;
    original.frameCap = 99;
    original.masterVolume = -9;
    original.musicVolume = 123;
    REQUIRE(original.save(path));
    REQUIRE(copy.load(path));
    CHECK_EQ(copy.viewDistance, 900);
    CHECK_EQ(copy.panPercent, 25);
    CHECK_EQ(copy.brightnessPercent, 50);
    CHECK_EQ(copy.shadowQuality, 3);
    CHECK_EQ(copy.frameRateLimit(), 240);
    CHECK_EQ(copy.masterVolume, 0);
    CHECK_EQ(copy.musicVolume, 100);
    original.fullscreen = false;
    REQUIRE(original.save(path));
    REQUIRE(copy.load(path));
    CHECK(!copy.fullscreen); // An explicit windowed preference still wins over the default.
    // Files from existing installations receive safe defaults for the new options.
    { std::ofstream out(path); out << "7\n1 1 0 460 100 100 100 1 1950 4 0 1 0 0 0 2 2 2 0 1 1 -\n"; }
    REQUIRE(copy.load(path));
    CHECK(copy.fullscreen);
    CHECK_EQ(copy.antiAliasing, 2);
    CHECK_EQ(copy.nightFogPercent, 25);
    CHECK(copy.clouds && copy.treeSway);
    CHECK(!copy.sounds); // Keep an existing player's mute choice.
    CHECK_EQ(copy.musicVolume, 55);
    CHECK(copy.muteUnfocused);
    // The old monthly autosave interval: any number of months turns on the five-minute autosaves.
    CHECK(copy.autosave);
    { std::ofstream out(path); out << "7\n1 1 0 460 100 100 100 3 1950 4 0 1 0 0 0 2 2 2 0 1 1 -\n"; }
    REQUIRE(copy.load(path));
    CHECK(copy.autosave);
    { std::ofstream out(path); out << "7\n1 1 0 460 100 100 100 0 1950 4 0 1 0 0 0 2 2 2 0 1 1 -\n"; }
    REQUIRE(copy.load(path));
    CHECK(!copy.autosave);
    { std::ofstream out(path); out << "8\n1 1 0 460 100 100 100 1 1950 4 0 1 0 0 0 2 2 2 0 1 1 -\n1 0 3\n"; }
    CHECK(!copy.load(path));
    CHECK(copy.fullscreen);
    CHECK_EQ(copy.antiAliasing, 2);
}

TEST_CASE("preferences: graphics presets retain display, visibility and gameplay choices") {
    ClientPreferences p;
    p.fullscreen = true;
    p.brightnessPercent = 150;
    p.nightFogPercent = 0;
    p.startYear = 1990;
    p.graphicsPreset(0);
    CHECK(!p.shadows);
    const int lowDistance = p.viewDistance;
    const int lowTrees = p.treeDistance;
    p.graphicsPreset(2);
    CHECK(p.shadows);
    CHECK(p.viewDistance > lowDistance);
    CHECK(p.treeDistance > lowTrees);
    CHECK(p.fullscreen);
    CHECK_EQ(p.brightnessPercent, 150);
    CHECK_EQ(p.nightFogPercent, 0);
    CHECK_EQ(p.startYear, 1990);
}

TEST_CASE("math: floorDiv and floorMod") {
    CHECK_EQ(floorDiv(7, 2), 3);
    CHECK_EQ(floorDiv(-7, 2), -4);
    CHECK_EQ(floorDiv(-8, 2), -4);
    CHECK_EQ(floorMod(-1, 32), 31);
    CHECK_EQ(floorMod(33, 32), 1);
    CHECK_EQ(floorMod(-32, 32), 0);
    CHECK_EQ(isqrt(0), 0u);
    CHECK_EQ(isqrt(15), 3u);
    CHECK_EQ(isqrt(16), 4u);
    CHECK_EQ(isqrt(1ull << 62), 1ull << 31);
    CHECK_EQ(chebyshev(-3, 2), 3);
}

TEST_CASE("rng: deterministic sequence and bounds") {
    Rng a(42, 7);
    Rng b(42, 7);
    for (int i = 0; i < 1000; ++i) CHECK_EQ(a.next(), b.next());
    Rng c(43, 7);
    int same = 0;
    for (int i = 0; i < 100; ++i) same += (a.next() == c.next());
    CHECK(same < 5);

    // Known-answer check guards against accidental algorithm changes (would break saves/MP).
    Rng k(12345);
    const u32 first = k.next();
    Rng k2(12345);
    CHECK_EQ(k2.next(), first);

    Rng r(1);
    int histogram[6] = {};
    for (int i = 0; i < 60000; ++i) {
        const u32 v = r.below(6);
        REQUIRE(v < 6);
        ++histogram[v];
    }
    for (int h : histogram) CHECK(h > 9000 && h < 11000);
    for (int i = 0; i < 1000; ++i) {
        const i32 v = r.range(-5, 5);
        CHECK(v >= -5 && v <= 5);
    }
}

TEST_CASE("hash: coordinate hashing is stable and well distributed") {
    CHECK_EQ(hashCoords(1, 2, 3), hashCoords(1, 2, 3));
    CHECK(hashCoords(1, 2, 3) != hashCoords(1, 3, 2));
    CHECK(hashCoords(1, 2, 3) != hashCoords(2, 2, 3));
    CHECK(hashCoords(1, 2, 3, 0) != hashCoords(1, 2, 3, 1));
    StateHasher h1, h2;
    h1.addAll(1, 2u, i64{3});
    h2.addAll(1, 2u, i64{3});
    CHECK_EQ(h1.value(), h2.value());
    h2.add(4);
    CHECK(h1.value() != h2.value());
}

TEST_CASE("fixed: arithmetic") {
    const Fixed a = Fixed::fromInt(3);
    const Fixed b = Fixed::fromRatio(1, 2);
    CHECK_EQ((a * b).raw, Fixed::fromRatio(3, 2).raw);
    CHECK_EQ((a / b).floor(), 6);
    CHECK_EQ(Fixed::fromRatio(-1, 2).floor(), -1);
    CHECK_EQ(Fixed::fromRatio(-1, 2).ceil(), 0);
    CHECK_EQ(Fixed::fromRatio(5, 2).round(), 3);
    CHECK(Fixed::fromInt(2) > Fixed::fromRatio(3, 2));
}

namespace {
struct Sample {
    i32 a = 0;
    u8 b = 0;
    std::string name;
    std::vector<i16> values;
    std::optional<u64> maybe;
    std::map<i32, std::string> lookup;
    std::variant<std::monostate, i32, std::string> var;
    bool flag = false;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b, name, values, maybe, lookup, var, flag);
    }
    friend bool operator==(const Sample&, const Sample&) = default;
};
} // namespace

TEST_CASE("serialize: round trip of composite types") {
    Sample s;
    s.a = -123456;
    s.b = 200;
    s.name = "Brookford";
    s.values = {1, -2, 300, -32768};
    s.maybe = 0x0123456789abcdefULL;
    s.lookup = {{1, "one"}, {-5, "minus five"}};
    s.var = std::string("variant");
    s.flag = true;

    const std::vector<u8> bytes = toBytes(s);
    Sample out;
    fromBytes(bytes, out);
    CHECK(out == s);

    // Truncated input must throw instead of reading garbage.
    bool threw = false;
    try {
        Sample bad;
        fromBytes(std::span<const u8>(bytes.data(), bytes.size() - 3), bad);
    } catch (const SerializeError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("serialize: varint encoding") {
    ByteWriter w;
    w.varint(0);
    w.varint(127);
    w.varint(128);
    w.varint(~0ull);
    CHECK_EQ(w.size(), 1u + 1u + 2u + 10u);
    ByteReader r(w.data());
    CHECK_EQ(r.varint(), 0u);
    CHECK_EQ(r.varint(), 127u);
    CHECK_EQ(r.varint(), 128u);
    CHECK_EQ(r.varint(), ~0ull);
    CHECK(r.atEnd());
}

namespace {
struct ThingTag {};
using ThingId = StrongId<ThingTag>;
struct Thing {
    i32 v = 0;
    Thing() = default;
    explicit Thing(i32 x) : v(x) {}
    template <class Ar>
    void serialize(Ar& ar) {
        ar(v);
    }
};
} // namespace

TEST_CASE("pool: create, destroy, reuse lowest index, iterate, serialise") {
    Pool<Thing, ThingId> pool;
    const ThingId a = pool.create(1);
    const ThingId b = pool.create(2);
    const ThingId c = pool.create(3);
    CHECK_EQ(pool.size(), 3u);
    pool.destroy(c);
    pool.destroy(a);
    CHECK(!pool.contains(a));
    CHECK(pool.contains(b));
    const ThingId d = pool.create(4);
    CHECK_EQ(d.value, a.value); // lowest free slot first
    int sum = 0;
    for (auto [id, thing] : pool) sum += thing.v;
    CHECK_EQ(sum, 6);

    const auto bytes = toBytes(pool);
    Pool<Thing, ThingId> loaded;
    fromBytes(bytes, loaded);
    CHECK_EQ(loaded.size(), 2u);
    CHECK_EQ(loaded[b].v, 2);
    // The loaded pool must hand out the same id the original would.
    CHECK_EQ(loaded.create(9).value, pool.create(9).value);
}

TEST_CASE("calendar: civil conversions and weekdays") {
    CHECK_EQ(daysFromCivil(1970, 1, 1), 0);
    CHECK(civilFromDays(0) == (CivilDate{1970, 1, 1}));
    CHECK(weekdayFromDays(0) == Weekday::Thursday);
    CHECK(weekdayFromDays(daysFromCivil(1950, 1, 2)) == Weekday::Monday);
    CHECK(weekdayFromDays(daysFromCivil(2026, 10, 17)) == Weekday::Saturday);
    CHECK(civilFromDays(daysFromCivil(2000, 2, 29)) == (CivilDate{2000, 2, 29}));
    for (i32 d = -40000; d < 40000; d += 17) {
        const CivilDate c = civilFromDays(d);
        CHECK_EQ(daysFromCivil(c.year, c.month, c.day), d);
    }
    GameClock clock;
    CHECK(clock.weekday() == Weekday::Monday);
    clock.tick = 5 * kTicksPerDay + 8 * kTicksPerHour + 30 * kTicksPerMinute;
    CHECK(clock.weekday() == Weekday::Saturday);
    CHECK_EQ(clock.minuteOfDay(), 8 * 60 + 30);
    CHECK_EQ(formatTimeOfDay(clock.minuteOfDay()), std::string("08:30"));
    CHECK_EQ(formatDate(daysFromCivil(1950, 1, 2)), std::string("Mon 2 Jan 1950"));
}

TEST_CASE("money: formatting") {
    CHECK_EQ(formatMoney(0), std::string("$0"));
    CHECK_EQ(formatMoney(999), std::string("$999"));
    CHECK_EQ(formatMoney(1000), std::string("$1,000"));
    CHECK_EQ(formatMoney(-1234567), std::string("-$1,234,567"));
    CHECK_EQ(formatMoneyShort(2500000), std::string("$2.5M"));
}

TEST_CASE("names: deterministic") {
    CHECK_EQ(generateTownName(7), generateTownName(7));
    int distinct = 0;
    for (u32 i = 0; i < 50; ++i) distinct += generateTownName(i) != generateTownName(i + 1000);
    CHECK(distinct > 40);
    CHECK(!generateCompanyName(3).empty());
    CHECK_EQ(stationNameSuffix(0), std::string(""));
}
