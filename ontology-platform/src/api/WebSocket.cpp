#include <ontology/WebSocket.hpp>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <cstring>
#include <openssl/sha.h>
#include <openssl/evp.h>

namespace ontology {

// ============================================================================
// WebSocketServer 实现
// ============================================================================

WebSocketServer::WebSocketServer(const Config& config) : config_(config) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start() {
    if (running_.load()) return false;

    running_.store(true);
    serverThread_ = std::thread(&WebSocketServer::serverLoop, this);

    return true;
}

void WebSocketServer::stop() {
    if (!running_.load()) return;

    running_.store(false);

    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        for (auto& [id, conn] : connections_) {
            if (conn.socketFd >= 0) {
                // 发送 close frame
                auto closeFrame = encodeFrame(WebSocketMessage::close());
                ::send(conn.socketFd, closeFrame.data(), closeFrame.size(), 0);
                ::close(conn.socketFd);
                conn.socketFd = -1;
            }
            conn.isOpen = false;
        }
        connections_.clear();
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void WebSocketServer::serverLoop() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) return;

    int flags = fcntl(serverSocket, F_GETFL, 0);
    fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK);

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config_.port);

    if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(serverSocket);
        return;
    }

    if (listen(serverSocket, 10) < 0) {
        ::close(serverSocket);
        return;
    }

    while (running_.load()) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);

        if (clientSocket >= 0) {
            if (connectionCount() >= config_.maxConnections) {
                ::close(clientSocket);
                continue;
            }

            std::thread connThread(&WebSocketServer::handleConnection, this,
                                   clientSocket, String(inet_ntoa(clientAddr.sin_addr)));
            connThread.detach();
        }

        usleep(10000);
    }

    ::close(serverSocket);
}

void WebSocketServer::handleConnection(int clientSocket, const String& remoteAddr) {
    int connectionId;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connectionId = nextConnectionId_++;

        WebSocketConnection conn;
        conn.id = connectionId;
        conn.socketFd = clientSocket;
        conn.remoteAddr = remoteAddr;
        conn.connectedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        conn.isOpen = true;

        connections_[connectionId] = conn;
    }

    if (onConnect_) {
        onConnect_(connectionId, remoteAddr);
    }

    // 读取握手请求
    char buffer[4096];
    ssize_t len = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            connections_.erase(connectionId);
        }
        ::close(clientSocket);
        if (onDisconnect_) onDisconnect_(connectionId, remoteAddr);
        return;
    }
    buffer[len] = '\0';
    String request(buffer);

    // 执行握手
    String response = performHandshake(request);
    if (response.empty()) {
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            connections_.erase(connectionId);
        }
        ::close(clientSocket);
        if (onDisconnect_) onDisconnect_(connectionId, remoteAddr);
        return;
    }

    ::send(clientSocket, response.c_str(), response.size(), 0);

    // 处理消息循环
    std::vector<uint8_t> recvBuffer;
    while (running_.load()) {
        char frameBuffer[4096];
        ssize_t frameLen = recv(clientSocket, frameBuffer, sizeof(frameBuffer), 0);

        if (frameLen <= 0) break;

        recvBuffer.insert(recvBuffer.end(), frameBuffer, frameBuffer + frameLen);

        // 尝试解码帧
        while (recvBuffer.size() >= 2) {
            WebSocketMessage message;
            size_t consumed;
            try {
                consumed = decodeFrame(recvBuffer, message);
            } catch (const std::exception& e) {
                spdlog::error("WebSocket frame decode error: {}", e.what());
                break;
            }

            if (consumed == 0) break; // incomplete frame, wait for more data

            recvBuffer.erase(recvBuffer.begin(), recvBuffer.begin() + consumed);

            if (message.type == WebSocketMessage::Type::Close) {
                goto connection_end;
            }
            else if (message.type == WebSocketMessage::Type::Ping) {
                auto pong = encodeFrame(WebSocketMessage::pong());
                ::send(clientSocket, pong.data(), pong.size(), 0);
            }
            else if (message.type == WebSocketMessage::Type::Text ||
                     message.type == WebSocketMessage::Type::Binary) {
                if (messageHandler_) {
                    messageHandler_(connectionId, message);
                }
            }
        }
    }

connection_end:
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connections_.erase(connectionId);
    }

    if (onDisconnect_) {
        onDisconnect_(connectionId, remoteAddr);
    }

    ::close(clientSocket);
}

String WebSocketServer::performHandshake(const String& request) {
    auto keyPos = request.find("Sec-WebSocket-Key:");
    if (keyPos == String::npos) return "";

    auto keyStart = request.find(" ", keyPos) + 1;
    auto keyEnd = request.find("\r\n", keyStart);
    String clientKey = request.substr(keyStart, keyEnd - keyStart);

    String magic = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    unsigned char sha1Hash[20];
    SHA1((unsigned char*)magic.c_str(), magic.size(), sha1Hash);

    char acceptKey[64];
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(acceptKey), sha1Hash, 20);
    String acceptKeyStr(acceptKey, 28);  // EVP_EncodeBlock outputs 28 chars for 20 bytes

    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " + acceptKeyStr + "\r\n"
           "\r\n";
}

std::vector<uint8_t> WebSocketServer::encodeFrame(const WebSocketMessage& message) {
    std::vector<uint8_t> frame;

    uint8_t opcode;
    switch (message.type) {
        case WebSocketMessage::Type::Text: opcode = 0x01; break;
        case WebSocketMessage::Type::Binary: opcode = 0x02; break;
        case WebSocketMessage::Type::Ping: opcode = 0x09; break;
        case WebSocketMessage::Type::Pong: opcode = 0x0A; break;
        case WebSocketMessage::Type::Close: opcode = 0x08; break;
    }

    frame.push_back(0x80 | opcode);

    size_t payloadLen = message.type == WebSocketMessage::Type::Binary ?
                        message.binaryData.size() : message.data.size();

    if (payloadLen < 126) {
        frame.push_back(static_cast<uint8_t>(payloadLen));
    } else if (payloadLen < 65536) {
        frame.push_back(126);
        frame.push_back((payloadLen >> 8) & 0xFF);
        frame.push_back(payloadLen & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((payloadLen >> (i * 8)) & 0xFF);
        }
    }

    if (message.type == WebSocketMessage::Type::Binary) {
        frame.insert(frame.end(), message.binaryData.begin(), message.binaryData.end());
    } else {
        frame.insert(frame.end(), message.data.begin(), message.data.end());
    }

    return frame;
}

size_t WebSocketServer::decodeFrame(const std::vector<uint8_t>& data, WebSocketMessage& outMessage) {
    if (data.size() < 2) return 0;

    uint8_t byte1 = data[0];
    uint8_t byte2 = data[1];

    uint8_t opcode = byte1 & 0x0F;
    bool masked = (byte2 & 0x80) != 0;
    size_t payloadLen = byte2 & 0x7F;

    size_t headerLen = 2;
    if (payloadLen == 126) {
        if (data.size() < 4) return 0;
        headerLen += 2;
        payloadLen = (static_cast<size_t>(data[2]) << 8) | data[3];
    } else if (payloadLen == 127) {
        if (data.size() < 10) return 0;
        headerLen += 8;
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
            payloadLen = (payloadLen << 8) | data[2 + i];
        }
    }

    std::vector<uint8_t> mask;
    if (masked) {
        headerLen += 4;
        if (data.size() < headerLen) return 0;
        mask = {data[headerLen - 4], data[headerLen - 3],
                data[headerLen - 2], data[headerLen - 1]};
    }

    if (data.size() < headerLen + payloadLen) return 0;

    std::vector<uint8_t> payload(data.begin() + headerLen,
                                 data.begin() + headerLen + payloadLen);

    if (masked) {
        for (size_t i = 0; i < payload.size(); i++) {
            payload[i] ^= mask[i % 4];
        }
    }

    switch (opcode) {
        case 0x01: outMessage.type = WebSocketMessage::Type::Text; break;
        case 0x02: outMessage.type = WebSocketMessage::Type::Binary; break;
        case 0x08: outMessage.type = WebSocketMessage::Type::Close; break;
        case 0x09: outMessage.type = WebSocketMessage::Type::Ping; break;
        case 0x0A: outMessage.type = WebSocketMessage::Type::Pong; break;
    }

    outMessage.data = String(payload.begin(), payload.end());
    outMessage.binaryData = payload;

    return headerLen + payloadLen;
}

bool WebSocketServer::sendOnSocket(int socketFd, const std::vector<uint8_t>& frame) {
    if (socketFd < 0) return false;

    size_t totalSent = 0;
    while (totalSent < frame.size()) {
        ssize_t sent = ::send(socketFd, frame.data() + totalSent,
                              frame.size() - totalSent, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        totalSent += static_cast<size_t>(sent);
    }
    return true;
}

bool WebSocketServer::send(int connectionId, const WebSocketMessage& message) {
    auto frame = encodeFrame(message);

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it == connections_.end() || !it->second.isOpen) return false;

    bool ok = sendOnSocket(it->second.socketFd, frame);
    if (!ok) {
        it->second.isOpen = false;
    }
    return ok;
}

void WebSocketServer::broadcast(const WebSocketMessage& message) {
    auto frame = encodeFrame(message);

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (auto& [id, conn] : connections_) {
        if (conn.isOpen && conn.socketFd >= 0) {
            if (!sendOnSocket(conn.socketFd, frame)) {
                conn.isOpen = false;
            }
        }
    }
}

void WebSocketServer::broadcastToTopic(const String& topic, const WebSocketMessage& message) {
    auto frame = encodeFrame(message);

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (auto& [id, conn] : connections_) {
        if (conn.isOpen && conn.socketFd >= 0 && conn.subscriptions.count(topic) > 0) {
            if (!sendOnSocket(conn.socketFd, frame)) {
                conn.isOpen = false;
            }
        }
    }
}

void WebSocketServer::notifyEvent(const WebSocketEvent& event) {
    auto message = WebSocketMessage::text(event.toJson().dump());
    broadcastToTopic(event.type, message);
    broadcastToTopic("all", message);
}

int WebSocketServer::connectionCount() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    return static_cast<int>(connections_.size());
}

std::vector<WebSocketConnection> WebSocketServer::getConnections() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    std::vector<WebSocketConnection> result;
    for (const auto& [id, conn] : connections_) {
        result.push_back(conn);
    }
    return result;
}

void WebSocketServer::setMessageHandler(MessageHandler handler) {
    messageHandler_ = std::move(handler);
}

void WebSocketServer::setConnectionHandler(ConnectionHandler onConnect, ConnectionHandler onDisconnect) {
    onConnect_ = std::move(onConnect);
    onDisconnect_ = std::move(onDisconnect);
}

void WebSocketServer::setErrorHandler(ErrorHandler handler) {
    errorHandler_ = std::move(handler);
}

void WebSocketServer::subscribe(int connectionId, const String& topic) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end()) {
        it->second.subscriptions.insert(topic);
    }
}

void WebSocketServer::unsubscribe(int connectionId, const String& topic) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end()) {
        it->second.subscriptions.erase(topic);
    }
}

void WebSocketServer::closeConnection(int connectionId, const String& reason) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end()) {
        if (it->second.socketFd >= 0) {
            auto closeFrame = encodeFrame(WebSocketMessage::close());
            sendOnSocket(it->second.socketFd, closeFrame);
            ::close(it->second.socketFd);
            it->second.socketFd = -1;
        }
        it->second.isOpen = false;
    }
}

// ============================================================================
// KnowledgeGraphPusher 实现
// ============================================================================

KnowledgeGraphPusher::KnowledgeGraphPusher(WebSocketServer* wsServer)
    : wsServer_(wsServer) {}

int64_t KnowledgeGraphPusher::currentTimeMillis() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void KnowledgeGraphPusher::pushTripleAdd(const Triple& triple) {
    WebSocketEvent event;
    event.type = "triple_add";
    event.timestamp = currentTimeMillis();
    event.data = {
        {"subject", triple.subject},
        {"predicate", triple.predicate},
        {"object", triple.object},
        {"confidence", triple.confidence}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushTripleRemove(const Triple& triple) {
    WebSocketEvent event;
    event.type = "triple_remove";
    event.timestamp = currentTimeMillis();
    event.data = {
        {"subject", triple.subject},
        {"predicate", triple.predicate},
        {"object", triple.object}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushEntityCreate(const String& type, const String& id, const Json& entity) {
    WebSocketEvent event;
    event.type = "entity_create";
    event.timestamp = currentTimeMillis();
    event.data = {
        {"entityType", type},
        {"id", id},
        {"entity", entity}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushEntityUpdate(const String& type, const String& id, const Json& changes) {
    WebSocketEvent event;
    event.type = "entity_update";
    event.timestamp = currentTimeMillis();
    event.data = {
        {"entityType", type},
        {"id", id},
        {"changes", changes}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushEntityDelete(const String& type, const String& id) {
    WebSocketEvent event;
    event.type = "entity_delete";
    event.timestamp = currentTimeMillis();
    event.data = {
        {"entityType", type},
        {"id", id}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushInferComplete(const String& entityId, const std::vector<Triple>& results) {
    WebSocketEvent event;
    event.type = "infer_complete";
    event.timestamp = currentTimeMillis();

    Json resultArray = Json::array();
    for (const auto& t : results) {
        resultArray.push_back({
            {"subject", t.subject},
            {"predicate", t.predicate},
            {"object", t.object}
        });
    }

    event.data = {
        {"entityId", entityId},
        {"results", resultArray},
        {"count", results.size()}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushConsistencyCheck(bool passed, const std::vector<String>& violations) {
    WebSocketEvent event;
    event.type = "consistency_check";
    event.timestamp = currentTimeMillis();

    Json arr = Json::array();
    for (const auto& v : violations) arr.push_back(v);

    event.data = {
        {"passed", passed},
        {"violations", arr}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushEmbeddingProgress(int epoch, int total, float loss) {
    WebSocketEvent event;
    event.type = "embedding_progress";
    event.timestamp = currentTimeMillis();
    event.data = {
        {"epoch", epoch},
        {"total", total},
        {"progress", static_cast<float>(epoch) / total * 100},
        {"loss", loss}
    };
    wsServer_->notifyEvent(event);
}

void KnowledgeGraphPusher::pushSystemStatus(const String& component, const String& status, const Json& details) {
    WebSocketEvent event;
    event.type = "system_status";
    event.timestamp = currentTimeMillis();
    event.data = {
        {"component", component},
        {"status", status},
        {"details", details}
    };
    wsServer_->notifyEvent(event);
}

// ============================================================================
// HttpWebSocketBridge 实现
// ============================================================================

HttpWebSocketBridge::HttpWebSocketBridge() {
    WebSocketServer::Config config;
    config.port = 8081;
    wsServer_ = std::make_unique<WebSocketServer>(config);
}

bool HttpWebSocketBridge::tryUpgrade(const String& path, const String& upgradeHeader,
                                      const String& secWebSocketKey, int& connectionId) {
    if (upgradeHeader != "websocket") return false;

    connectionId = 0;
    connectionPaths_[connectionId] = path;
    return true;
}

void HttpWebSocketBridge::handleFrame(int connectionId, const std::vector<uint8_t>& frame) {
    if (!wsServer_) return;
    WebSocketMessage message;
    wsServer_->decodeFrame(frame, message);
    // Could forward to message handler
}

WebSocketServer* HttpWebSocketBridge::getWebSocketServer() {
    return wsServer_.get();
}

} // namespace ontology
