#pragma once

#include <cstdint>
#include <string_view>

namespace tp {

inline constexpr std::string_view kGameName = "transport-poly";
inline constexpr std::string_view kVersionString = "0.1.0";

// Bumped whenever the network protocol or simulation rules change in an incompatible way.
// Clients and servers must match exactly because the simulation runs in lockstep.
inline constexpr std::uint32_t kProtocolVersion = 14;

} // namespace tp
