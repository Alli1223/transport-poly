#include "core/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace tp::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex;
std::ofstream g_file;

std::string_view levelName(Level level) {
    switch (level) {
    case Level::Trace: return "trace";
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
    }
    return "?";
}

} // namespace

void setLevel(Level level) { g_level.store(level, std::memory_order_relaxed); }

Level level() { return g_level.load(std::memory_order_relaxed); }

bool openFile(const std::string& path) {
    std::scoped_lock lock(g_mutex);
    g_file.close();
    g_file.open(path, std::ios::out | std::ios::trunc);
    return g_file.is_open();
}

void write(Level lvl, std::string_view message) {
    using namespace std::chrono;
    const auto now = floor<milliseconds>(system_clock::now());
    const auto tod = now - floor<days>(now);
    const std::string line = std::format("[{:%H:%M:%S}] [{}] {}\n", hh_mm_ss<milliseconds>(tod), levelName(lvl),
                                         message);
    std::scoped_lock lock(g_mutex);
    std::fputs(line.c_str(), stderr);
    if (g_file.is_open()) {
        g_file << line;
        if (lvl >= Level::Warn) g_file.flush();
    }
}

} // namespace tp::log
