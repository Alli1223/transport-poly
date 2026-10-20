#include "test.hpp"

#include "core/json.hpp"
#include "core/release_feed.hpp"
#include "core/sha256.hpp"

#include <string>
#include <vector>

using namespace tp;

TEST_CASE("updates: JSON values, escapes and nesting") {
    const auto v = parseJson(R"({"a": 1.5, "b": [true, false, null], "s": "x\"y\\z\né🚂", "o": {"k": "v"}})");
    REQUIRE(v.has_value());
    CHECK(v->num("a") == 1.5);
    REQUIRE(v->get("b") != nullptr);
    CHECK_EQ(v->get("b")->array.size(), 3u);
    CHECK(v->get("b")->array[0].boolean);
    CHECK_EQ(v->str("s"), std::string("x\"y\\z\n\xC3\xA9\xF0\x9F\x9A\x82"));
    CHECK_EQ(v->get("o")->str("k"), std::string("v"));
    CHECK(!parseJson("{\"a\": }").has_value());
    CHECK(!parseJson("[1, 2").has_value());
    CHECK(!parseJson("{} trailing").has_value());
    for (const auto invalid : {"+1", "01", "1.", "1e9999", "[.5]", "[1e]", "\"\n\"", "\"\\uD800\""})
        CHECK(!parseJson(invalid).has_value());
}

TEST_CASE("updates: SHA-256 digests") {
    const std::string empty;
    CHECK_EQ(sha256Hex(std::span<const u8>(reinterpret_cast<const u8*>(empty.data()), 0)),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    const std::string abc = "abc";
    CHECK_EQ(sha256Hex(std::span<const u8>(reinterpret_cast<const u8*>(abc.data()), abc.size())),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    // Longer than one block, fed in uneven pieces.
    const std::string text(1000, 'a');
    Sha256 s;
    for (usize at = 0; at < text.size(); at += 37)
        s.update(std::span<const u8>(reinterpret_cast<const u8*>(text.data()) + at, std::min<usize>(37, text.size() - at)));
    const auto digest = s.finish();
    CHECK_EQ(sha256Hex(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size())).substr(0, 2),
             std::string(1, "0123456789abcdef"[digest[0] >> 4]) + "0123456789abcdef"[digest[0] & 15]);
    CHECK_EQ(sha256Hex(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size())),
             std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
}

TEST_CASE("updates: versions compare part by part, with pre-releases first") {
    CHECK(releases::compareVersions("0.2.0", "0.1.0") > 0);
    CHECK(releases::compareVersions("v0.1.0", "0.1.0") == 0);
    CHECK(releases::compareVersions("1.10.0", "1.9.9") > 0);
    CHECK(releases::compareVersions("1.0", "1.0.0") == 0);
    CHECK(releases::compareVersions("1.0.0-rc1", "1.0.0") < 0);
    CHECK(releases::compareVersions("1.0.0-rc2", "1.0.0-rc1") > 0);
    CHECK(releases::compareVersions("0.9.0", "0.10.0") < 0);
    CHECK(releases::compareVersions("1.0.0+build.9", "1.0.0") == 0);
    CHECK(releases::compareVersions("1.0.0-rc.10", "1.0.0-rc.2") > 0);
    CHECK(releases::compareVersions("1.0.0-2", "1.0.0-beta") < 0);
}

TEST_CASE("updates: the latest release, its downloads and their checksums") {
    const std::string json = R"({
        "tag_name": "v0.2.0", "name": "transport-poly 0.2.0", "html_url": "https://example.com/r/v0.2.0",
        "body": "### Multiplayer\n- A company lobby",
        "assets": [
            {"name": "SHA256SUMS.txt", "browser_download_url": "https://example.com/sums", "size": 300},
            {"name": "transport-poly-0.2.0-linux-x64.tar.gz", "browser_download_url": "https://example.com/linux", "size": 1000},
            {"name": "transport-poly-0.2.0-windows-x64.zip", "browser_download_url": "https://example.com/zip", "size": 2000},
            {"name": "transport-poly-0.2.0-windows-x64-setup.exe", "browser_download_url": "https://example.com/setup", "size": 3000}
        ]})";
    const auto r = releases::parseRelease(json);
    REQUIRE(r.has_value());
    CHECK_EQ(r->version, std::string("0.2.0"));
    CHECK_EQ(r->title, std::string("transport-poly 0.2.0"));
    CHECK(r->notes.find("company lobby") != std::string::npos);
    REQUIRE(releases::pickAsset(*r, releases::Kind::WindowsInstaller) != nullptr);
    CHECK_EQ(releases::pickAsset(*r, releases::Kind::WindowsInstaller)->url, std::string("https://example.com/setup"));
    CHECK_EQ(releases::pickAsset(*r, releases::Kind::WindowsPortable)->size, 2000u);
    CHECK_EQ(releases::pickAsset(*r, releases::Kind::Linux)->name, std::string("transport-poly-0.2.0-linux-x64.tar.gz"));
    REQUIRE(releases::checksums(*r) != nullptr);
    CHECK(!releases::parseRelease("{\"name\": \"no tag\"}").has_value());

    const std::string sums = "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3  transport-poly-0.2.0-linux-x64.tar.gz\r\n"
                             "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD *transport-poly-0.2.0-windows-x64.zip\n";
    CHECK_EQ(releases::checksumFor(sums, "transport-poly-0.2.0-linux-x64.tar.gz").value_or(""),
             std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
    CHECK_EQ(releases::checksumFor(sums, "transport-poly-0.2.0-windows-x64.zip").value_or(""),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(!releases::checksumFor(sums, "missing.zip").has_value());
    CHECK(!releases::checksumFor(std::string(64, 'x') + "  file.zip", "file.zip").has_value());
}

TEST_CASE("updates: malformed versions and unsafe download names are rejected") {
    CHECK(!releases::parseRelease(R"({"tag_name":"../../v1.0.0"})"));
    CHECK(!releases::parseRelease(R"({"tag_name":"1.x.0"})"));
    CHECK(!releases::parseRelease(R"({"tag_name":"1.0.0","draft":true})"));
    CHECK(!releases::parseRelease(R"({"tag_name":"1.0.0","prerelease":true})"));
    releases::Release release;
    release.version = "1.0.0";
    release.assets = {{"../transport-poly-1.0.0-windows-x64.zip", "https://example.com/archive", 20},
                      {"transport-poly-2.0.0-windows-x64.zip", "https://example.com/wrong", 20}};
    CHECK(!releases::pickAsset(release, releases::Kind::WindowsPortable));
    const auto unsafe = releases::parseRelease(R"({"tag_name":"1.0.0","html_url":"file:///bad","assets":[
      {"name":"transport-poly-1.0.0-windows-x64.zip","browser_download_url":"file:///bad","size":12}]})");
    REQUIRE(unsafe.has_value());
    CHECK(unsafe->assets.empty());
    CHECK(unsafe->page.empty());
}
