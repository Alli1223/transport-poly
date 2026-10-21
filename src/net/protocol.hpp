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
    Cursor,         // CursorMsg: where the player is pointing
    SetCompanyPassword, // SetCompanyPasswordMsg
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
    Speed,          // SpeedMsg: sent even while paused
    JoinRefused,    // JoinRefusedMsg: a company could not be joined (the connection stays open)
    Cursors         // CursorsMsg: where every player is pointing
};

struct HelloMsg {
    u32 protocol = 0;
    std::string name;
    std::string password; // the server's, if it has one
    template <class Ar>
    void serialize(Ar& ar) {
        ar(protocol, name, password);
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

// Who a chat message is for: everyone, the sender's company (co-op partners) or one player.
enum class ChatScope : u8 { All, Company, Private };

struct ChatMsg {
    std::string text;
    ChatScope scope = ChatScope::All;
    u16 target = 0; // client id, for private messages
    template <class Ar>
    void serialize(Ar& ar) {
        ar(text, scope, target);
    }
};

inline constexpr u8 kJoinNewCompany = 0xFF;
inline constexpr u8 kJoinSpectator = 0xFE;
inline constexpr u8 kJoinAsk = 0xFD; // client side only: let the player choose once the map is loaded

struct JoinCompanyMsg {
    u8 company = kJoinNewCompany; // a company id, kJoinNewCompany or kJoinSpectator
    std::string name;             // a new company's name (empty: one is made up)
    u8 colour = 0xFF;             // a new company's livery (0xFF: the first free one)
    std::string password;         // an existing company's password, if it has one
    template <class Ar>
    void serialize(Ar& ar) {
        ar(company, name, colour, password);
    }
};

// Locks the sender's company with a password co-op partners must give to join it (empty unlocks).
struct SetCompanyPasswordMsg {
    std::string password;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(password);
    }
};

// A world position in 1/64 tiles.
inline constexpr i32 kCursorScale = 64;

struct CursorMsg {
    bool visible = false; // false when the pointer is over the interface or off the map
    i32 x = 0, y = 0, z = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(visible, x, y, z);
    }
};

struct CursorInfo {
    u16 client = 0;
    i32 x = 0, y = 0, z = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(client, x, y, z);
    }
};

struct CursorsMsg {
    std::vector<CursorInfo> cursors;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(cursors);
    }
};

struct JoinRefusedMsg {
    std::string reason;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(reason);
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
    ChatScope scope = ChatScope::All;
    u16 fromClient = 0;
    CompanyId fromCompany;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, text, scope, fromClient, fromCompany);
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
    std::vector<CompanyId> locked; // companies that need a password to join
    template <class Ar>
    void serialize(Ar& ar) {
        ar(clients, locked);
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
