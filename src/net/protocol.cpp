#include "net/protocol.hpp"

#include <cstring>

namespace tp::net {

void Connection::sendRaw(PacketType type, std::span<const u8> payload) {
    if (!error_.empty()) return;
    const u32 len = static_cast<u32>(payload.size() + 1);
    const usize start = out_.size();
    out_.resize(start + 5 + payload.size());
    u8* p = out_.data() + start;
    p[0] = static_cast<u8>(len);
    p[1] = static_cast<u8>(len >> 8);
    p[2] = static_cast<u8>(len >> 16);
    p[3] = static_cast<u8>(len >> 24);
    p[4] = static_cast<u8>(type);
    if (!payload.empty()) std::memcpy(p + 5, payload.data(), payload.size());
}

bool Connection::pump() {
    if (!error_.empty() || !socket_.valid()) return false;
    // Send.
    while (outPos_ < out_.size()) {
        const i64 n = socket_.send(out_.data() + outPos_, out_.size() - outPos_);
        if (n < 0) {
            close("connection lost while sending");
            return false;
        }
        if (n == 0) break;
        outPos_ += static_cast<usize>(n);
    }
    if (outPos_ == out_.size()) {
        out_.clear();
        outPos_ = 0;
    } else if (outPos_ > (1u << 20)) {
        out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(outPos_));
        outPos_ = 0;
    }
    // Receive.
    u8 buf[65536];
    for (;;) {
        const i64 n = socket_.recv(buf, sizeof(buf));
        if (n < 0) {
            close("connection closed");
            return false;
        }
        if (n == 0) break;
        in_.insert(in_.end(), buf, buf + n);
    }
    return true;
}

std::optional<Packet> Connection::next() {
    const usize avail = in_.size() - inPos_;
    if (avail < 5) {
        if (inPos_ > 0 && avail == 0) {
            in_.clear();
            inPos_ = 0;
        }
        return std::nullopt;
    }
    const u8* h = in_.data() + inPos_;
    const u32 len = static_cast<u32>(h[0]) | (static_cast<u32>(h[1]) << 8) | (static_cast<u32>(h[2]) << 16) |
                    (static_cast<u32>(h[3]) << 24);
    if (len == 0 || len > kMaxPacketSize) {
        close("malformed packet");
        return std::nullopt;
    }
    if (avail < 4 + static_cast<usize>(len)) {
        // Compact before waiting for the rest of a large packet.
        if (inPos_ > (1u << 16)) {
            in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(inPos_));
            inPos_ = 0;
        }
        return std::nullopt;
    }
    Packet p;
    p.type = static_cast<PacketType>(h[4]);
    p.payload.assign(h + 5, h + 4 + len);
    inPos_ += 4 + len;
    if (inPos_ == in_.size()) {
        in_.clear();
        inPos_ = 0;
    }
    return p;
}

void Connection::close(const std::string& reason) {
    if (error_.empty()) error_ = reason.empty() ? "closed" : reason;
    socket_.close();
}

} // namespace tp::net
