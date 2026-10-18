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
    };

    explicit NetServer(std::unique_ptr<World> world, Options options = {});
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
    // Human readable events (joins, leaves, errors) since the last call.
    std::vector<std::string> takeEvents();
    std::vector<ChatBroadcastMsg> takeChat();

private:
    struct Client {
        u16 id = 0;
        Connection conn;
        std::string name;
        CompanyId company;
        bool welcomed = false;
        double exploreBudget = 4.0;
    };

    void acceptClients();
    void handle(Client& c, const Packet& p);
    void sendMap(Client& c);
    void broadcastRaw(PacketType type, const std::vector<u8>& payload);
    void broadcastClientList();
    void collectResults();

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
};

} // namespace tp::net
