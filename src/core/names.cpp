#include "core/names.hpp"

#include "core/hash.hpp"

#include <array>
#include <string_view>

namespace tp {

namespace {

constexpr std::array<std::string_view, 68> kPrefixes = {
    "Ash",    "Bal",   "Bar",   "Bel",   "Black",  "Brad",  "Bram",  "Brook", "Burn",   "Cam",   "Car",   "Ches",
    "Clay",   "Cold",  "Dal",   "Dun",   "East",   "Elm",   "Fair",  "Fal",   "Fen",    "Glen",  "Gold",  "Green",
    "Hal",    "Har",   "Hart",  "Hay",   "High",   "Hol",   "Kings", "Lang",  "Lea",    "Lin",   "Long",  "Mar",
    "Mel",    "Mill",  "Mor",   "New",   "North",  "Oak",   "Pen",   "Port",  "Queens", "Red",   "Rich",  "Ros",
    "Salt",   "Sand",  "Shep",  "South", "Stan",   "Stone", "Stow",  "Sun",   "Thorn",  "Wal",   "Wat",   "West",
    "Whit",   "Wil",   "Win",   "Wood",  "Wor",    "Ald",   "Brent", "Crow"};

constexpr std::array<std::string_view, 34> kSuffixes = {
    "bridge", "brook", "bury",   "by",    "caster", "combe", "dale",     "den",   "field", "ford",  "gate", "ham",
    "haven",  "hill",  "holm",   "hurst", "ington", "leigh", "ley",      "mere",  "minster", "mouth", "ness", "pool",
    "port",   "stead", "stone",  "thorpe", "ton",   "well",  "wick",     "wood",  "worth", "chester"};

constexpr std::array<std::string_view, 6> kTownQualifiers = {"Upper ", "Lower ", "Great ", "Little ", "Old ", "St. "};

constexpr std::array<std::string_view, 48> kFirstNames = {
    "Alice", "Amir",   "Anna",  "Ben",   "Carla",  "Chen",   "Chloe", "Daniel", "Elena", "Emma",   "Felix", "Fiona",
    "George", "Grace", "Hana",  "Harry", "Isla",   "Ivan",   "Jack",  "Jade",   "Kai",   "Kate",   "Leo",   "Lily",
    "Luca",  "Maya",   "Mohammed", "Nina", "Noah", "Olivia", "Omar",  "Priya",  "Quinn", "Rosa",   "Ryan",  "Sam",
    "Sara",  "Sofia",  "Tom",   "Uma",   "Victor", "Wei",    "Will",  "Xavier", "Yara",  "Yusuf",  "Zoe",   "Ella"};

constexpr std::array<std::string_view, 40> kSurnames = {
    "Abbott", "Baker",  "Brown",   "Carter", "Clarke", "Cole",   "Davies", "Edwards", "Evans",  "Fisher",
    "Foster", "Garcia", "Green",   "Hall",   "Harris", "Hughes", "Jones",  "Khan",    "King",   "Lee",
    "Lewis",  "Martin", "Mason",   "Moore",  "Morgan", "Murphy", "Nguyen", "Patel",   "Price",  "Reed",
    "Roberts", "Scott", "Shaw",    "Smith",  "Taylor", "Turner", "Walker", "Ward",    "Wright", "Young"};

constexpr std::array<std::string_view, 8> kCompanyForms = {" Transport", " Transit", " & Co. Railways", " Lines",
                                                           " Coaches",   " Express", " Rail & Bus", " Connect"};

constexpr std::array<std::string_view, 16> kStationSuffixes = {
    "",       " Central", " North",  " South", " East",     " West",    " Market", " Park",
    " Bridge", " Square", " Halt",   " Cross", " Junction", " Gardens", " Quay",   " Heights"};

template <usize N>
std::string_view pick(const std::array<std::string_view, N>& list, u32 h) {
    return list[h % N];
}

} // namespace

std::string generateTownName(u32 seed) {
    const u32 h1 = mix32(seed ^ 0x51ed270bU);
    const u32 h2 = mix32(h1 + 0x2545f491U);
    const u32 h3 = mix32(h2 + 0x9e3779b9U);
    std::string name;
    if (h3 % 11 == 0) name += pick(kTownQualifiers, h3 >> 8);
    name += pick(kPrefixes, h1);
    std::string_view suffix = pick(kSuffixes, h2);
    // Avoid doubled letters at the seam like "Wellleigh".
    if (!name.empty() && name.back() == suffix.front()) suffix = pick(kSuffixes, h2 >> 9);
    name += suffix;
    return name;
}

std::string generatePersonFirstName(u32 seed) { return std::string(pick(kFirstNames, mix32(seed ^ 0x1234567U))); }

std::string generatePersonSurname(u32 seed) { return std::string(pick(kSurnames, mix32(seed ^ 0x7654321U))); }

std::string generateCompanyName(u32 seed) {
    const u32 h = mix32(seed ^ 0xC0FFEEU);
    return std::string(pick(kSurnames, h)) + std::string(pick(kCompanyForms, h >> 12));
}

std::string stationNameSuffix(u32 index) {
    if (index < kStationSuffixes.size()) return std::string(kStationSuffixes[index]);
    return " " + std::to_string(index - kStationSuffixes.size() + 2);
}

} // namespace tp
