#pragma once
// GLSL sources embedded in the executable.

#include <string>

namespace tp::gfx::shaders {

std::string terrainVS();
std::string terrainFS();
std::string modelVS();
std::string modelFS();
std::string waterVS();
std::string waterFS();
std::string skyVS();
std::string skyFS();
std::string cloudVS();
std::string cloudFS();
std::string glowVS();
std::string glowFS();
std::string shadowStaticVS();
std::string shadowModelVS();
std::string shadowFS();
std::string overlayVS();
std::string overlayFS();

} // namespace tp::gfx::shaders
