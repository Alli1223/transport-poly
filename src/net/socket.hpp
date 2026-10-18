#pragma once
// Minimal cross-platform non-blocking TCP sockets (Winsock 2 / BSD sockets).

#include "core/types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace tp::net {

// Must be called once before using sockets (WSAStartup on Windows). Safe to call repeatedly.
bool initNetworking();

class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(Socket&& o) noexcept;
    Socket& operator=(Socket&& o) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Starts a non-blocking connection attempt; poll finishConnect() until it returns != 0.
    static std::optional<Socket> beginConnect(const std::string& host, u16 port, std::string& error);
    // 1 = connected, 0 = still connecting, -1 = failed (error filled).
    int finishConnect(std::string& error);

    static std::optional<Socket> listen(u16 port, std::string& error);
    // Accepts a pending connection, if any.
    std::optional<Socket> accept();

    // Number of bytes sent, 0 if the call would block, -1 on error.
    i64 send(const void* data, usize size);
    // Number of bytes received, 0 if no data is available, -1 if the connection closed or failed.
    i64 recv(void* data, usize size);

    bool valid() const { return fd_ != kInvalid; }
    // Actual bound port, including the OS-selected port after listen(0); zero if unbound.
    u16 localPort() const;
    void close();
    const std::string& peer() const { return peer_; }

private:
    static constexpr std::intptr_t kInvalid = -1;
    explicit Socket(std::intptr_t fd) : fd_(fd) {}

    std::intptr_t fd_ = kInvalid;
    bool connecting_ = false;
    std::string peer_;
};

} // namespace tp::net
