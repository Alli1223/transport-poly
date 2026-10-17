#include "core/log.hpp"
#include "core/version.hpp"

#include <cstdlib>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    tp::log::info("{} dedicated server {} (protocol {})", tp::kGameName, tp::kVersionString,
                  tp::kProtocolVersion);
    tp::log::info("Nothing to serve yet - networking arrives in milestone M10.");
    return EXIT_SUCCESS;
}
