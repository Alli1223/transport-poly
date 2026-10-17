#include "core/types.hpp"

#include "core/log.hpp"

#include <cstdlib>

namespace tp {

void assertFailed(const char* expression, const char* file, int line) {
    log::error("Assertion failed: {} ({}:{})", expression, file, line);
    std::abort();
}

} // namespace tp
