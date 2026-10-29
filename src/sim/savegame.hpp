#pragma once
// Save game files and in-memory world snapshots (used to send the map to joining clients).

#include "core/types.hpp"
#include "sim/ids.hpp"
#include "sim/world.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tp {

struct SaveInfo {
    std::string path;
    std::string companyName;
    i32 day = 0;
    u64 seed = 0;
    i64 fileTime = 0; // seconds since epoch of the last write
};

// Serialises the complete world (plus the id of the local player's company) into bytes.
std::vector<u8> snapshotWorld(const World& world, CompanyId localCompany);
// Restores a world from snapshot bytes. Throws SerializeError on malformed input.
std::unique_ptr<World> restoreWorld(std::span<const u8> data, CompanyId& localCompany);

bool saveGame(const World& world, CompanyId localCompany, const std::string& path, std::string& error);
std::unique_ptr<World> loadGame(const std::string& path, CompanyId& localCompany, std::string& error);

// Lists the save files in a directory, newest first.
std::vector<SaveInfo> listSaves(const std::string& directory);

// Rolling autosaves are autosave-1 to autosave-<slots> in a directory. The next one to write is the
// first that does not exist yet, or else the one written longest ago.
std::string nextAutosavePath(const std::string& directory, int slots);
bool isAutosave(const std::string& path);

} // namespace tp
