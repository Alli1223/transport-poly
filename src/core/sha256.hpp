#pragma once
// SHA-256, for checking downloaded updates against the checksums published with each release.

#include "core/types.hpp"

#include <array>
#include <span>
#include <string>

namespace tp {

class Sha256 {
public:
    Sha256();
    void update(std::span<const u8> data);
    std::array<u8, 32> finish();

private:
    void block(const u8* p);
    std::array<u32, 8> h_;
    std::array<u8, 64> buffer_{};
    usize buffered_ = 0;
    u64 length_ = 0;
};

// Lower case hexadecimal digest of `data`.
std::string sha256Hex(std::span<const u8> data);

} // namespace tp
