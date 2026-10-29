#include "sim/savegame.hpp"
#include "test.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

using namespace tp;

TEST_CASE("autosaves: five rolling slots fill in order, then the oldest is overwritten") {
    const auto dir = std::filesystem::temp_directory_path() /
                     std::format("transport-poly-autosaves-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{dir};
    std::filesystem::create_directories(dir);
    const std::string d = dir.string();
    const auto slot = [&](int n) { return std::format("{}/autosave-{}.tpsave", d, n); };
    // Each save is stamped a minute after the one before, so the order never rests on clock resolution.
    const auto start = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    int written = 0;
    const auto write = [&](const std::string& path) {
        { std::ofstream out(path, std::ios::binary); out << "save"; }
        std::filesystem::last_write_time(path, start + std::chrono::minutes(++written));
    };
    for (int n = 1; n <= 5; ++n) {
        const std::string next = nextAutosavePath(d, 5);
        CHECK_EQ(next, slot(n));
        write(next);
    }
    for (int n = 1; n <= 7; ++n) {
        const std::string next = nextAutosavePath(d, 5);
        CHECK_EQ(next, slot((n - 1) % 5 + 1));
        write(next);
    }
    // A slot the player deleted is filled before an older one is overwritten.
    std::filesystem::remove(slot(4));
    CHECK_EQ(nextAutosavePath(d, 5), slot(4));
    CHECK(isAutosave(slot(2)));
    CHECK(isAutosave(d + "/autosave.tpsave"));
    CHECK(!isAutosave(d + "/my-railway.tpsave"));
}
