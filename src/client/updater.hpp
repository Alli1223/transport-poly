#pragma once
// Self updating: checks the release feed in the background, downloads the update that suits how the
// game was installed, checks it against the release's published SHA-256, and hands over to the
// Windows installer, or to a small script that unpacks the new version over this one once the game
// has quit and starts it again.

#include "core/release_feed.hpp"
#include "core/types.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tp::update {

// The running executable, and how it was installed (the Windows installer leaves its uninstaller
// next to the game).
std::filesystem::path executablePath();
releases::Kind installKind();
// Where an installed game keeps saves and settings (it may not write where it is installed).
std::filesystem::path userDataDir();
// Opens a web page in the browser.
bool openUrl(const std::string& url);
// Fetches a URL (https or http). `got` counts the bytes as they arrive.
bool httpGet(const std::string& url, std::vector<u8>& out, std::string& error, std::atomic<u64>* got = nullptr,
             bool json = false);

class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };

    ~Updater();
    // Looks for a release newer than `currentVersion` (in the background).
    void check(const std::string& feedUrl, const std::string& currentVersion);
    // Downloads and verifies the update for this installation (in the background).
    void download();
    // Starts the installer or the update script. The game must quit straight after a true return.
    bool apply();

    State state() const;
    std::string error() const;
    releases::Release release() const;
    u64 downloaded() const { return got_; }
    u64 total() const { return total_; }

private:
    void join();
    void fail(const std::string& why);

    mutable std::mutex mutex_;
    std::thread worker_;
    State state_ = State::Idle;
    std::string error_;
    releases::Release release_;
    std::filesystem::path file_; // the downloaded update
    std::atomic<u64> got_{0}, total_{0};
};

} // namespace tp::update
