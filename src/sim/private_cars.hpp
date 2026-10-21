#pragma once
#include "sim/person.hpp"
#include <optional>
namespace tp::privatecars {
bool ownsCar(u32 serial, i32 year);
// Citizens drive on any connected road: around their town, and to work or visits in other towns
// along roads through the countryside. Searches give up after this many tiles.
inline constexpr i32 kMaxRouteTiles = 40'000;
std::optional<std::vector<TilePos>> route(const World& w, TilePos from2, TilePos to2);
bool begin(World& w, PersonId id, TilePos from2, TilePos to2);
// Returns true while travelling (including queues); false once the road journey ends.
bool advance(World& w, PersonId id);
void release(World& w, PersonId id);
void rebuild(World& w);
Dir entry(const Person& p);
Dir exit(const Person& p);
i32 progress(const Person& p, i64 tick);
}
