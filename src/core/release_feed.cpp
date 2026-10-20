#include "core/release_feed.hpp"

#include "core/json.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace tp::releases {

namespace {

bool downloadUrl(std::string_view url) {
    return url.starts_with("https://") || url.starts_with("http://127.0.0.1:") || url.starts_with("http://localhost:");
}

bool validVersion(std::string_view version) {
    if (version.empty() || version.size() > 128) return false;
    for (const unsigned char c : version)
        if (!std::isalnum(c) && c != '.' && c != '-' && c != '+') return false;
    const auto core = version.substr(0, version.find_first_of("-+"));
    usize at = 0;
    for (int part = 0; part < 3; ++part) {
        const auto end = core.find('.', at);
        const auto number = core.substr(at, end == std::string_view::npos ? end : end - at);
        u64 value = 0;
        const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
        if (number.empty() || result.ec != std::errc{} || result.ptr != number.data() + number.size()) return false;
        if (part == 2) return end == std::string_view::npos;
        if (end == std::string_view::npos) return false;
        at = end + 1;
    }
    return false;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

int compareVersions(std::string_view a, std::string_view b) {
    if (!a.empty() && (a[0] == 'v' || a[0] == 'V')) a.remove_prefix(1);
    if (!b.empty() && (b[0] == 'v' || b[0] == 'V')) b.remove_prefix(1);
    a = a.substr(0, a.find('+'));
    b = b.substr(0, b.find('+'));
    const auto split = [](std::string_view& v) {
        std::string_view pre;
        if (const usize dash = v.find('-'); dash != std::string_view::npos) {
            pre = v.substr(dash + 1);
            v = v.substr(0, dash);
        }
        return pre;
    };
    const std::string_view preA = split(a), preB = split(b);
    // Numbers compared part by part; missing parts count as zero.
    for (;;) {
        const auto part = [](std::string_view& v) {
            u64 n = 0;
            const usize dot = v.find('.');
            const std::string_view head = v.substr(0, dot);
            std::from_chars(head.data(), head.data() + head.size(), n);
            v = dot == std::string_view::npos ? std::string_view{} : v.substr(dot + 1);
            return n;
        };
        if (a.empty() && b.empty()) break;
        const u64 x = part(a), y = part(b);
        if (x != y) return x < y ? -1 : 1;
    }
    if (preA.empty() != preB.empty()) return preA.empty() ? 1 : -1;
    auto x = preA, y = preB;
    while (!x.empty() || !y.empty()) {
        if (x.empty() || y.empty()) return x.empty() ? -1 : 1;
        const auto take = [](std::string_view& text) {
            const auto dot = text.find('.');
            const auto head = text.substr(0, dot);
            text = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
            return head;
        };
        const auto left = take(x), right = take(y);
        const auto numeric = [](std::string_view text) {
            return !text.empty() && std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c); });
        };
        const bool ln = numeric(left), rn = numeric(right);
        if (ln != rn) return ln ? -1 : 1;
        if (ln && left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
        if (left != right) return left < right ? -1 : 1;
    }
    return 0;
}

std::optional<Release> parseRelease(std::string_view json) {
    const std::optional<JsonValue> doc = parseJson(json);
    if (!doc || doc->type != JsonValue::Type::Object) return std::nullopt;
    for (const auto flag : {"draft", "prerelease"})
        if (const auto* v = doc->get(flag); v && v->boolean) return std::nullopt;
    Release r;
    std::string tag = doc->str("tag_name");
    if (tag.empty()) return std::nullopt;
    if (tag[0] == 'v' || tag[0] == 'V') tag.erase(0, 1);
    if (!validVersion(tag)) return std::nullopt;
    r.version = tag;
    r.title = doc->str("name", "transport-poly " + tag);
    r.notes = doc->str("body");
    r.page = doc->str("html_url");
    if (!downloadUrl(r.page)) r.page.clear();
    if (const JsonValue* assets = doc->get("assets"); assets && assets->type == JsonValue::Type::Array)
        for (const JsonValue& a : assets->array) {
            Asset asset;
            asset.name = a.str("name");
            asset.url = a.str("browser_download_url");
            const double size = a.num("size");
            if (size < 0 || size > 1024.0 * 1024.0 * 1024.0) continue;
            asset.size = static_cast<u64>(size);
            if (!asset.name.empty() && downloadUrl(asset.url)) r.assets.push_back(std::move(asset));
        }
    return r;
}

const Asset* pickAsset(const Release& release, Kind kind) {
    const std::string_view suffix = kind == Kind::WindowsInstaller ? "-windows-x64-setup.exe"
                                    : kind == Kind::WindowsPortable ? "-windows-x64.zip"
                                                                    : "-linux-x64.tar.gz";
    for (const Asset& a : release.assets)
        if (validVersion(release.version) && lower(a.name) == lower("transport-poly-" + release.version + std::string(suffix))) return &a;
    return nullptr;
}

const Asset* checksums(const Release& release) {
    for (const Asset& a : release.assets)
        if (lower(a.name) == "sha256sums.txt") return &a;
    return nullptr;
}

std::optional<std::string> checksumFor(std::string_view sums, std::string_view file) {
    usize at = 0;
    while (at < sums.size()) {
        usize end = sums.find('\n', at);
        if (end == std::string_view::npos) end = sums.size();
        std::string_view line = sums.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const usize space = line.find(' ');
        if (space == std::string_view::npos || space != 64) continue;
        std::string_view name = line.substr(space);
        while (!name.empty() && (name.front() == ' ' || name.front() == '*')) name.remove_prefix(1);
        if (name == file && std::ranges::all_of(line.substr(0, 64), [](unsigned char c) { return std::isxdigit(c); })) return lower(line.substr(0, 64));
    }
    return std::nullopt;
}

} // namespace tp::releases
