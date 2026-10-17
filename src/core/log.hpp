#pragma once
// Thread-safe logging to stderr and (optionally) a log file.

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace tp::log {

enum class Level { Trace, Debug, Info, Warn, Error };

void setLevel(Level level);
Level level();

// Mirrors all subsequent log output into the given file (truncated on open).
bool openFile(const std::string& path);

void write(Level level, std::string_view message);

template <class... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Trace) write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Debug) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Info) write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Warn) write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace tp::log
