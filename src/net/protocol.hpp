#pragma once
// Wire protocol: packet framing over TCP and the message types exchanged between server and
// clients. Every packet is [u32 length][u8 type][payload], length counting type + payload.

#include "core/serialize.hpp"
#include "core/types.hpp"
#include "net/socket.hpp"
#include "sim/commands.hpp"

#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tp::net {

inline constexpr u16 kDefaultPort = 3979;
inline constexpr u32 kMaxPacketSize = 4u << 20;
inline constexpr usize kMapChunkSize = 256u << 10;

enum class PacketType : u8 {
    // client -> server
    Hello = 1,      // HelloMsg
    CommandRequest, // CommandRequestMsg
    Chat,           // ChatMsg
    JoinCompany,    // JoinCompanyMsg
    // server -> client
    Welcome = 64,   // WelcomeMsg
    Reject,         // RejectMsg
    MapBegin,       // MapBeginMsg
    MapData,        // raw bytes
    MapEnd,         // (empty)
    Commands,       // CommandsMsg: commands executed at the start of `tick`
    Frame,          // FrameMsg: clients may simulate up to and including `tick`
    ChatBroadcast,  // ChatBroadcastMsg
    ClientList,     // ClientListMsg
    CompanyAssigned, // CompanyAssignedMsg
    Speed           // SpeedMsg: sent even while paused
};

struct HelloMsg {
    u32 protocol = 0;
    std::string name;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(protocol, name);
    }
};

struct CommandRequestMsg {
    u32 seq = 0;
    CommandPayload payload;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(seq, payload);
    }
};

struct ChatMsg {
    std::string text;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(text);
    }
};

inline constexpr u8 kJoinNewCompany = 0xFF;
inline constexpr u8 kJoinSpectator = 0xFE;

struct JoinCompanyMsg {
    u8 company = kJoinNewCompany;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(company);
    }
};

struct WelcomeMsg {
    u16 clientId = 0;
    std::string serverName;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(clientId, serverName);
    }
};

struct RejectMsg {
    std::string reason;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(reason);
    }
};

struct MapBeginMsg {
    u64 size = 0;
    i64 tick = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(size, tick);
    }
};

struct CommandsMsg {
    i64 tick = 0;
    std::vector<Command> commands;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tick, commands);
    }
};

struct FrameMsg {
    i64 tick = 0;
    u64 hash = 0;      // state hash after the tick, or 0 when not checked this frame
    u8 speed = 1;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(tick, hash, speed);
    }
};

struct SpeedMsg {
    u8 speed = 1;
    template <class Ar>
    void serialize(Ar& ar) { ar(speed); }
};

struct ChatBroadcastMsg {
    std::string from;
    std::string text;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, text);
    }
};

struct ClientInfo {
    u16 id = 0;
    std::string name;
    CompanyId company;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(id, name, company);
    }
};

struct ClientListMsg {
    std::vector<ClientInfo> clients;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(clients);
    }
};

struct CompanyAssignedMsg {
    CompanyId company;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(company);
    }
};

struct Packet {
    PacketType type;
    std::vector<u8> payload;

    template <class T>
    T decode() const {
        T msg;
        fromBytes(payload, msg);
        return msg;
    }
};

// Buffered, framed packet stream over a socket.
class Connection {
public:
    Connection() = default;
    explicit Connection(Socket socket) : socket_(std::move(socket)) {}

    template <class T>
    void send(PacketType type, const T& msg) {
        sendRaw(type, toBytes(msg));
    }
    void sendRaw(PacketType type, std::span<const u8> payload);
    // Sends buffered data and receives into the input buffer. Returns false once the connection
    // is closed (see error()).
    bool pump();
    std::optional<Packet> next();

    bool open() const { return socket_.valid() && error_.empty(); }
    const std::string& error() const { return error_; }
    usize pendingBytes() const { return out_.size() - outPos_; }
    Socket& socket() { return socket_; }
    void close(const std::string& reason);

private:
    Socket socket_;
    std::vector<u8> in_;
    usize inPos_ = 0;
    std::vector<u8> out_;
    usize outPos_ = 0;
    std::string error_;
};

} // namespace tp::net
