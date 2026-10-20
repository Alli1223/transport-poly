#pragma once
// Network layer around the authoritative Server: accepts clients, sends them a snapshot of the
// world, stamps their command requests with their company and broadcasts the command stream and
// frame markers that keep every peer's simulation in lockstep.

#include "net/protocol.hpp"
#include "net/server.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tp::net {

class NetServer {
public:
    struct Options {
        u16 port = kDefaultPort;
        std::string name = "transport-poly server";
        i32 hashInterval = 32; // ticks between state hash checks
        i32 maxClients = 16;
        std::string password;  // players must give it to join (empty: anyone may)
    };

    explicit NetServer(std::unique_ptr<World> world);
    NetServer(std::unique_ptr<World> world, Options options);
    ~NetServer();

    bool start(std::string& error);
    void update(double dtSeconds);
    void stop();

    Server& server() { return server_; }
    const Options& options() const { return options_; }

    // Commands from a player running inside the server process (the host).
    u32 submitLocal(CompanyId company, CommandPayload payload);
    std::vector<ClientInfo> clients() const;
    void setLocalHost(CompanyId company, const std::string& name = "Host");
    void broadcastChat(const std::string& from, const std::string& text);
    // Chat from the host player, to everyone, its company or one player.
    void localChat(const std::string& text, ChatScope scope = ChatScope::All, u16 target = 0);
    // Human readable events (joins, leaves, errors) since the last call.
    std::vector<std::string> takeEvents();
    // Chat the host player can see.
    std::vector<ChatBroadcastMsg> takeChat();

    // Co-op: a company's password, which players must give to join it (empty: none).
    void setCompanyPassword(CompanyId company, const std::string& password);
    bool companyLocked(CompanyId company) const;
    std::vector<CompanyId> lockedCompanies() const;
    // Where the host player is pointing (nullopt: nowhere), and every player's pointer.
    void setLocalCursor(const std::optional<CursorInfo>& cursor);
    std::vector<CursorInfo> cursors() const;
    // Disconnects a player, telling them why.
    bool kick(u16 client, const std::string& reason);
    // Moves the host player to another company (or makes them a spectator).
    void setLocalCompany(CompanyId company);

private:
    struct Client {
        u16 id = 0;
        Connection conn;
        std::string name;
        CompanyId company;
        bool welcomed = false;
        double exploreBudget = 4.0;
        std::optional<CursorInfo> cursor;
    };

    void acceptClients();
    void handle(Client& c, const Packet& p);
    void sendMap(Client& c);
    void broadcastRaw(PacketType type, const std::vector<u8>& payload);
    void broadcastClientList();
    void collectResults();
    void deliverChat(const ChatBroadcastMsg& msg, u16 target);
    void joinCompany(Client& c, const JoinCompanyMsg& msg);
    void assign(Client& c, CompanyId company);

    Server server_;
    Options options_;
    std::optional<Socket> listener_;
    std::vector<std::unique_ptr<Client>> clients_;
    u16 nextClientId_ = 2; // 0 = server, 1 = local host player
    u32 nextLocalSeq_ = 1;
    int lastBroadcastSpeed_ = -1;
    std::optional<ClientInfo> localHost_;
    std::vector<std::string> events_;
    std::vector<ChatBroadcastMsg> chat_;
    std::map<u8, std::string> companyPasswords_;
    std::optional<CursorInfo> localCursor_;
    double cursorTimer_ = 0.0;
    std::vector<CursorInfo> lastCursors_;
};

} // namespace tp::net
