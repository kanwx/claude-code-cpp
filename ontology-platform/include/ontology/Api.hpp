#pragma once

#include "ApiHandler.hpp"
#include "WebSocket.hpp"
#include <httplib.h>
#include <memory>
#include <vector>

namespace ontology {

// Type aliases (moved from old Api.hpp)
using WebSocketServerPtr = std::unique_ptr<WebSocketServer>;
using KnowledgeGraphPusherPtr = std::unique_ptr<KnowledgeGraphPusher>;

// ============================================================================
// HTTP API Server
// ============================================================================

class HttpServer {
public:
    struct Config {
        String host = "0.0.0.0";
        int port = 8080;
        bool cors = true;
        String jwtSecret;
        int timeout = 30;
    };

    HttpServer(const Config& config);
    HttpServer(int port);
    ~HttpServer();

    /// Set the shared service context (propagates to all handlers).
    void setContext(ServiceContextPtr ctx);

    /// Add a route handler module.
    void addHandler(std::shared_ptr<ApiHandler> handler);

    /// Get the WebSocket server (for real-time push).
    WebSocketServer* getWebSocketServer();

    /// Get the knowledge graph pusher.
    KnowledgeGraphPusher* getKnowledgeGraphPusher();

    /// Start the server (blocking).
    bool start();

    /// Stop the server.
    void stop();

private:
    int port_;
    std::unique_ptr<httplib::Server> server_;
    ServiceContextPtr ctx_;
    std::vector<std::shared_ptr<ApiHandler>> handlers_;

    // WebSocket real-time push
    WebSocketServerPtr wsServer_;
    KnowledgeGraphPusherPtr kgPusher_;
};

} // namespace ontology
