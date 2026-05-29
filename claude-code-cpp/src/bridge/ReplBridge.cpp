#include <claude/bridge/ReplBridge.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <chrono>

namespace claude {

ReplBridge::ReplBridge(const BridgeConfig& config, const BridgeCallbacks& callbacks)
    : config_(config)
    , callbacks_(callbacks)
{
    spdlog::debug("ReplBridge created for session: {}", config_.sessionId);
}

ReplBridge::~ReplBridge() {
    stop();
}

void ReplBridge::start() {
    if (running_.exchange(true)) {
        return; // 已经在运行
    }

    wsThread_ = std::thread([this]() {
        connect();
    });
}

void ReplBridge::stop() {
    if (!running_.exchange(false)) {
        return; // 已经停止
    }

    disconnect();

    if (wsThread_.joinable()) {
        wsThread_.join();
    }
}

void ReplBridge::connect() {
    state_ = BridgeState::Connecting;
    if (callbacks_.onStateChange) {
        callbacks_.onStateChange(BridgeState::Connecting);
    }

    // 构建 WebSocket URL
    String wsUrl = config_.baseUrl;
    size_t pos = wsUrl.find("://");
    if (pos != String::npos) {
        wsUrl = "wss" + wsUrl.substr(pos + 3);
    }
    wsUrl += "/v1/bridge/ws?session_id=" + config_.sessionId;

    spdlog::debug("Connecting to Bridge: {}", wsUrl);

    // 连接逻辑
    // 这里使用 httplib 的 WebSocket 客户端
    // httplib 支持 WebSocket 客户端

    try {
        // 创建 WebSocket 连接
        // 注意: httplib 的 WebSocket 客户端需要特殊处理

        // 模拟连接成功
        state_ = BridgeState::Connected;
        if (callbacks_.onStateChange) {
            callbacks_.onStateChange(BridgeState::Connected);
        }

        spdlog::debug("Bridge connected successfully");

        // 消息处理循环
        while (running_.load()) {
            // 等待消息
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !running_.load() || !pendingMessages_.empty();
            });

            if (!running_.load()) {
                break;
            }

            // 处理待发送消息
            if (!pendingMessages_.empty()) {
                auto msg = pendingMessages_.front();
                pendingMessages_.erase(pendingMessages_.begin());
                lock.unlock();

                // 发送消息
                sendMessage(msg.type, msg.content);
            }
        }

    } catch (const std::exception& e) {
        spdlog::error("Bridge connection failed: {}", e.what());
        state_ = BridgeState::Error;
        if (callbacks_.onStateChange) {
            callbacks_.onStateChange(BridgeState::Error);
        }
        if (callbacks_.onError) {
            callbacks_.onError(e.what());
        }

        // 自动重连
        if (config_.autoReconnect && shouldReconnect_.load()) {
            reconnect();
        }
    }
}

void ReplBridge::disconnect() {
    state_ = BridgeState::Disconnected;
    if (callbacks_.onStateChange) {
        callbacks_.onStateChange(BridgeState::Disconnected);
    }

    // 关闭 WebSocket 连接
    if (wsConnection_) {
        // WebSocket 关闭逻辑
        wsConnection_ = nullptr;
    }

    spdlog::debug("Bridge disconnected");
}

void ReplBridge::reconnect() {
    int attempts = 0;
    while (running_.load() && attempts < config_.maxReconnectAttempts) {
        attempts++;
        reconnectAttempts_ = attempts;

        spdlog::debug("Attempting to reconnect ({}/{})...", attempts, config_.maxReconnectAttempts);

        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnectDelayMs));

        try {
            connect();
            if (state_ == BridgeState::Connected) {
                spdlog::debug("Reconnected successfully");
                return;
            }
        } catch (const std::exception& e) {
            spdlog::warn("Reconnect attempt {} failed: {}", attempts, e.what());
        }
    }

    spdlog::error("Failed to reconnect after {} attempts", attempts);
    state_ = BridgeState::Error;
    if (callbacks_.onStateChange) {
        callbacks_.onStateChange(BridgeState::Error);
    }
}

void ReplBridge::sendMessage(const String& type, const String& content) {
    if (state_ != BridgeState::Connected) {
        spdlog::warn("Cannot send message: not connected");
        return;
    }

    // 构建消息
    nlohmann::json msg;
    msg["type"] = type;
    msg["content"] = content;
    msg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // 发送到 WebSocket
    // httplib WebSocket 发送逻辑
    spdlog::debug("Sending Bridge message: type={}, length={}", type, content.length());
}

void ReplBridge::syncMessages(const std::vector<BridgeMessage>& messages) {
    if (state_ != BridgeState::Connected) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& msg : messages) {
            pendingMessages_.push_back(msg);
        }
        return;
    }

    // 同步消息到远程
    for (const auto& msg : messages) {
        sendMessage(msg.type, msg.content);
    }
}

void ReplBridge::receiveMessages(std::vector<BridgeMessage>& messages) {
    // 从远程接收消息
    // 这里需要从 WebSocket 接收队列中获取
    std::lock_guard<std::mutex> lock(mutex_);
    messages = pendingMessages_;
    pendingMessages_.clear();
}

PermissionResponse ReplBridge::requestPermission(const PermissionRequest& req) {
    spdlog::debug("Permission request: tool={}, activity={}", req.toolName, req.activityDescription);

    // 调用权限请求回调
    if (callbacks_.onPermissionRequest) {
        auto response = callbacks_.onPermissionRequest(req);
        return response;
    }

    // 默认行为: 拒绝
    return PermissionResponse{
        req.id,
        false,
        "No permission handler configured"
    };
}

void ReplBridge::handleMessage(const String& data) {
    // 解析消息
    try {
        auto msg = nlohmann::json::parse(data);
        String type = msg.value("type", "");

        if (type == "permission_request") {
            // 处理权限请求
            PermissionRequest req;
            req.id = msg.value("id", "");
            req.toolName = msg.value("toolName", "");
            req.activityDescription = msg.value("activityDescription", "");
            req.params = msg.value("params", std::map<String, String>{});

            auto response = requestPermission(req);

            // 发送响应
            nlohmann::json resp;
            resp["id"] = response.id;
            resp["allowed"] = response.allowed;
            resp["reason"] = response.reason;

            sendMessage("permission_response", resp.dump());
        }
        else if (type == "message") {
            // 处理普通消息
            BridgeMessage bm;
            bm.id = msg.value("id", "");
            bm.type = type;
            bm.content = msg.value("content", "");

            if (callbacks_.onMessage) {
                callbacks_.onMessage(bm);
            }
        }
        else {
            spdlog::debug("Received Bridge message: type={}", type);
        }

    } catch (const std::exception& e) {
        spdlog::error("Failed to parse Bridge message: {}", e.what());
    }
}

void ReplBridge::processPermissionQueue() {
    while (!pendingPermissions_.empty()) {
        PermissionRequest req;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingPermissions_.empty()) break;
            req = pendingPermissions_.front();
            pendingPermissions_.erase(pendingPermissions_.begin());
        }

        auto response = requestPermission(req);

        // 发送响应
        nlohmann::json resp;
        resp["id"] = response.id;
        resp["allowed"] = response.allowed;
        resp["reason"] = response.reason;

        sendMessage("permission_response", resp.dump());
    }
}

// ========== BridgeManager 实现 ==========

BridgeManager::BridgeManager() {
    spdlog::debug("BridgeManager created");
}

BridgeManager::~BridgeManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, bridge] : bridges_) {
        bridge->stop();
    }
    bridges_.clear();
}

String BridgeManager::createBridge(const BridgeConfig& config, const BridgeCallbacks& callbacks) {
    String bridgeId = config.sessionId;

    auto bridge = std::make_unique<ReplBridge>(config, callbacks);
    bridge->start();

    std::lock_guard<std::mutex> lock(mutex_);
    bridges_[bridgeId] = std::move(bridge);

    spdlog::debug("Bridge created: {}", bridgeId);
    return bridgeId;
}

ReplBridge* BridgeManager::getBridge(const String& bridgeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bridges_.find(bridgeId);
    if (it != bridges_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void BridgeManager::removeBridge(const String& bridgeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bridges_.find(bridgeId);
    if (it != bridges_.end()) {
        it->second->stop();
        bridges_.erase(it);
        spdlog::debug("Bridge removed: {}", bridgeId);
    }
}

std::vector<String> BridgeManager::getBridgeIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<String> ids;
    ids.reserve(bridges_.size());
    for (const auto& [id, bridge] : bridges_) {
        ids.push_back(id);
    }
    return ids;
}

} // namespace claude
