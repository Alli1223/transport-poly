#pragma once
// Reading the release feed (GitHub's "latest release" API response): the newest version, its notes,
// its downloads and their published checksums, and which download suits this copy of the game.

#include "core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tp::releases {

struct Asset {
    std::string name;
    std::string url;
    u64 size = 0;
};

struct Release {
    std::string version; // without the leading "v"
    std::string title;
    std::string notes;   // Markdown
    std::string page;    // where to read about it in a browser
    std::vector<Asset> assets;
};

// Negative, zero or positive as version a is older, the same as or newer than b ("1.10.0" > "1.9.2";
// a pre-release suffix such as "-rc1" sorts before the release itself).
int compareVersions(std::string_view a, std::string_view b);

std::optional<Release> parseRelease(std::string_view json);

// How this copy of the game was installed, which decides the download that updates it.
enum class Kind { WindowsInstaller, WindowsPortable, Linux };

// The download for that kind: the Windows setup program, the Windows zip or the Linux tarball.
const Asset* pickAsset(const Release& release, Kind kind);
// The checksums file published with the release, if any.
const Asset* checksums(const Release& release);
// The SHA-256 a checksums file ("<hex>  <file name>" lines) lists for `file`, lower case.
std::optional<std::string> checksumFor(std::string_view sums, std::string_view file);

} // namespace tp::releases
