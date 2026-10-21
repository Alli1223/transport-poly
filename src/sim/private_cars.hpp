#pragma once
#include "sim/person.hpp"
#include <optional>
namespace tp::privatecars {
bool ownsCar(u32 serial, i32 year);
std::optional<std::vector<TilePos>> route(const World& w, TownId town, TilePos from2, TilePos to2);
bool begin(World& w, PersonId id, TilePos from2, TilePos to2);
// Returns true while travelling (including queues); false once the road journey ends.
bool advance(World& w, PersonId id);
void release(World& w, PersonId id);
void rebuild(World& w);
Dir entry(const Person& p);
Dir exit(const Person& p);
i32 progress(const Person& p, i64 tick);
}
