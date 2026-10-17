#include "core/png.hpp"

#include <array>
#include <cstdio>
#include <vector>

namespace tp {

namespace {

std::array<u32, 256> makeCrcTable() {
    std::array<u32, 256> table{};
    for (u32 n = 0; n < 256; ++n) {
        u32 c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[n] = c;
    }
    return table;
}

u32 crc32(const u8* data, usize len, u32 crc = 0) {
    static const std::array<u32, 256> table = makeCrcTable();
    crc = ~crc;
    for (usize i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v >> 24));
    out.push_back(static_cast<u8>(v >> 16));
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

void chunk(std::vector<u8>& out, const char* type, const std::vector<u8>& data) {
    put32(out, static_cast<u32>(data.size()));
    const usize start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start));
}

} // namespace

bool writePng(const std::string& path, int width, int height, int channels, std::span<const u8> pixels) {
    if (width <= 0 || height <= 0 || (channels != 3 && channels != 4)) return false;
    const usize rowBytes = static_cast<usize>(width) * static_cast<usize>(channels);
    if (pixels.size() < rowBytes * static_cast<usize>(height)) return false;

    // Raw scanlines with filter byte 0.
    std::vector<u8> raw;
    raw.reserve((rowBytes + 1) * static_cast<usize>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const u8* row = pixels.data() + rowBytes * static_cast<usize>(y);
        raw.insert(raw.end(), row, row + rowBytes);
    }

    // zlib stream made of stored deflate blocks.
    std::vector<u8> z;
    z.push_back(0x78);
    z.push_back(0x01);
    usize pos = 0;
    do {
        const usize n = std::min<usize>(65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<u8>(n & 0xFF));
        z.push_back(static_cast<u8>(n >> 8));
        z.push_back(static_cast<u8>(~n & 0xFF));
        z.push_back(static_cast<u8>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    u32 a = 1, b = 0;
    for (const u8 v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);

    std::vector<u8> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<u8> ihdr;
    put32(ihdr, static_cast<u32>(width));
    put32(ihdr, static_cast<u32>(height));
    ihdr.push_back(8);
    ihdr.push_back(channels == 4 ? 6 : 2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

} // namespace tp
