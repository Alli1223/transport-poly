#include "net/socket.hpp"

#include <cstring>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using NativeSocket = SOCKET;
namespace {
constexpr NativeSocket kNativeInvalid = INVALID_SOCKET;
int lastError() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK; }
bool connectInProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
void closeNative(NativeSocket s) { closesocket(s); }
bool setNonBlocking(NativeSocket s) {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}
constexpr int kSendFlags = 0;
} // namespace
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
namespace {
constexpr NativeSocket kNativeInvalid = -1;
int lastError() { return errno; }
bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
bool connectInProgress(int e) { return e == EINPROGRESS || e == EALREADY; }
void closeNative(NativeSocket s) { ::close(s); }
bool setNonBlocking(NativeSocket s) {
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
constexpr int kSendFlags = MSG_NOSIGNAL;
} // namespace
#endif

namespace tp::net {

namespace {

NativeSocket native(std::intptr_t fd) { return static_cast<NativeSocket>(fd); }

void setNoDelay(NativeSocket s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
}

std::string errorText(int code) { return "socket error " + std::to_string(code); }

std::string describe(const sockaddr* addr) {
    char host[NI_MAXHOST] = {};
    char serv[NI_MAXSERV] = {};
    const socklen_t len = addr->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    if (getnameinfo(addr, len, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        return std::string(host) + ":" + serv;
    return "?";
}

} // namespace

bool initNetworking() {
#ifdef _WIN32
    static bool done = false;
    if (done) return true;
    WSADATA data;
    done = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return done;
#else
    return true;
#endif
}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& o) noexcept
    : fd_(std::exchange(o.fd_, kInvalid)), connecting_(o.connecting_), peer_(std::move(o.peer_)) {}

Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        close();
        fd_ = std::exchange(o.fd_, kInvalid);
        connecting_ = o.connecting_;
        peer_ = std::move(o.peer_);
    }
    return *this;
}

void Socket::close() {
    if (fd_ != kInvalid) {
        closeNative(native(fd_));
        fd_ = kInvalid;
    }
}

std::optional<Socket> Socket::beginConnect(const std::string& host, u16 port, std::string& error) {
    initNetworking();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0 || !result) {
        error = "cannot resolve " + host;
        return std::nullopt;
    }
    for (addrinfo* ai = result; ai; ai = ai->ai_next) {
        const NativeSocket s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == kNativeInvalid) continue;
        setNonBlocking(s);
        setNoDelay(s);
        const int r = ::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
        if (r == 0 || connectInProgress(lastError())) {
            Socket sock(static_cast<std::intptr_t>(s));
            sock.connecting_ = r != 0;
            sock.peer_ = describe(ai->ai_addr);
            freeaddrinfo(result);
            return sock;
        }
        closeNative(s);
    }
    freeaddrinfo(result);
    error = "cannot connect to " + host + ":" + service;
    return std::nullopt;
}

int Socket::finishConnect(std::string& error) {
    if (!valid()) {
        error = "invalid socket";
        return -1;
    }
    if (!connecting_) return 1;
#ifdef _WIN32
    fd_set wset, eset;
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    FD_SET(native(fd_), &wset);
    FD_SET(native(fd_), &eset);
    timeval tv{0, 0};
    const int n = select(0, nullptr, &wset, &eset, &tv);
    if (n < 0) {
        error = errorText(lastError());
        return -1;
    }
    if (n == 0) return 0;
    if (FD_ISSET(native(fd_), &eset)) {
        int err = 0;
        int len = sizeof(err);
        getsockopt(native(fd_), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        error = "connection refused (" + std::to_string(err) + ")";
        return -1;
    }
#else
    pollfd p{native(fd_), POLLOUT, 0};
    const int n = ::poll(&p, 1, 0);
    if (n < 0) {
        error = errorText(lastError());
        return -1;
    }
    if (n == 0) return 0;
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(native(fd_), SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        error = "connection failed (" + std::to_string(err) + ")";
        return -1;
    }
#endif
    connecting_ = false;
    return 1;
}

std::optional<Socket> Socket::listen(u16 port, std::string& error) {
    initNetworking();
    const NativeSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNativeInvalid) {
        error = errorText(lastError());
        return std::nullopt;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(s, 16) != 0) {
        error = "cannot listen on port " + std::to_string(port) + " (" + errorText(lastError()) + ")";
        closeNative(s);
        return std::nullopt;
    }
    setNonBlocking(s);
    Socket sock(static_cast<std::intptr_t>(s));
    sock.peer_ = "listen:" + std::to_string(port);
    return sock;
}

u16 Socket::localPort() const {
    if (!valid()) return 0;
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (getsockname(native(fd_), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
    return ntohs(address.sin_port);
}

std::optional<Socket> Socket::accept() {
    if (!valid()) return std::nullopt;
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    const NativeSocket s = ::accept(native(fd_), reinterpret_cast<sockaddr*>(&addr), &len);
    if (s == kNativeInvalid) return std::nullopt;
    setNonBlocking(s);
    setNoDelay(s);
    Socket sock(static_cast<std::intptr_t>(s));
    sock.peer_ = describe(reinterpret_cast<sockaddr*>(&addr));
    return sock;
}

i64 Socket::send(const void* data, usize size) {
    if (!valid() || connecting_) return 0;
    const int n = ::send(native(fd_), static_cast<const char*>(data), static_cast<int>(size), kSendFlags);
    if (n >= 0) return n;
    return wouldBlock(lastError()) ? 0 : -1;
}

i64 Socket::recv(void* data, usize size) {
    if (!valid() || connecting_) return 0;
    const int n = ::recv(native(fd_), static_cast<char*>(data), static_cast<int>(size), 0);
    if (n > 0) return n;
    if (n == 0) return -1; // orderly shutdown
    return wouldBlock(lastError()) ? 0 : -1;
}

} // namespace tp::net
