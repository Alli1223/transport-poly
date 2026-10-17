#pragma once
// Minimal PNG encoder (stored deflate blocks, no compression library needed).

#include "core/types.hpp"

#include <span>
#include <string>

namespace tp {

// Writes an 8-bit RGB or RGBA image. Rows are top to bottom.
bool writePng(const std::string& path, int width, int height, int channels, std::span<const u8> pixels);

} // namespace tp
