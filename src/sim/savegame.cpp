#include "sim/savegame.hpp"

#include "core/serialize.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

namespace tp {

namespace {

constexpr u32 kMagic = 0x56535054; // "TPSV" little endian

struct Header {
    u32 magic = kMagic;
    u32 version = kSaveVersion;
    CompanyId localCompany;
    std::string companyName;
    i32 day = 0;
    u64 seed = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(magic, version, localCompany, companyName, day, seed);
    }
};

Header makeHeader(const World& world, CompanyId localCompany) {
    Header h;
    h.localCompany = localCompany;
    if (const Company* c = world.companies.tryGet(localCompany)) h.companyName = c->name;
    h.day = world.clock.day();
    h.seed = world.settings.seed;
    return h;
}

Header readHeader(ByteReader& in) {
    Header h;
    Reader r(in, kSaveVersion);
    r(h);
    if (h.magic != kMagic) throw SerializeError("not a transport-poly save game");
    if (h.version < kMinSaveVersion || h.version > kSaveVersion)
        throw SerializeError("save game version " + std::to_string(h.version) + " is not supported (expected " +
                             std::to_string(kMinSaveVersion) + " to " + std::to_string(kSaveVersion) + ")");
    return h;
}

} // namespace

std::vector<u8> snapshotWorld(const World& world, CompanyId localCompany) {
    ByteWriter bw;
    Writer w(bw, kSaveVersion);
    Header h = makeHeader(world, localCompany);
    w(h);
    w(world);
    return bw.take();
}

std::unique_ptr<World> restoreWorld(std::span<const u8> data, CompanyId& localCompany) {
    ByteReader in(data);
    const Header h = readHeader(in);
    auto world = std::make_unique<World>();
    Reader r(in, h.version);
    r(*world);
    if (!in.atEnd()) throw SerializeError("trailing data after world");
    localCompany = h.localCompany;
    return world;
}

bool saveGame(const World& world, CompanyId localCompany, const std::string& path, std::string& error) {
    try {
        const std::vector<u8> bytes = snapshotWorld(world, localCompany);
        const std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
        // Write to a temporary file first so a crash never leaves a truncated save behind.
        const std::filesystem::path tmp = p.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "cannot write " + tmp.string();
                return false;
            }
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) {
                error = "write failed";
                return false;
            }
        }
        std::filesystem::rename(tmp, p);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::unique_ptr<World> loadGame(const std::string& path, CompanyId& localCompany, std::string& error) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = "cannot open " + path;
            return nullptr;
        }
        const std::vector<u8> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return restoreWorld(bytes, localCompany);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

std::vector<SaveInfo> listSaves(const std::string& directory) {
    std::vector<SaveInfo> out;
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tpsave") continue;
        SaveInfo info;
        info.path = entry.path().string();
        try {
            std::ifstream in(entry.path(), std::ios::binary);
            std::vector<u8> head(4096);
            in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<usize>(in.gcount()));
            ByteReader br(head);
            const Header h = readHeader(br);
            info.companyName = h.companyName;
            info.day = h.day;
            info.seed = h.seed;
        } catch (const std::exception&) {
            info.companyName = "(unreadable)";
        }
        const auto ftime = std::filesystem::last_write_time(entry.path(), ec);
        info.fileTime = std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.fileTime > b.fileTime; });
    return out;
}

std::string nextAutosavePath(const std::string& directory, int slots) {
    const auto slot = [&](int n) { return std::format("{}/autosave-{}.tpsave", directory, n); };
    std::string oldest = slot(1);
    auto oldestTime = std::filesystem::file_time_type::max();
    for (int n = 1; n <= slots; ++n) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(slot(n), ec);
        if (ec) return slot(n);
        if (time < oldestTime) {
            oldestTime = time;
            oldest = slot(n);
        }
    }
    return oldest;
}

bool isAutosave(const std::string& path) { return std::filesystem::path(path).stem().string().starts_with("autosave"); }

} // namespace tp
