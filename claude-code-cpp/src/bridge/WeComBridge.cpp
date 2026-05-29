#include <claude/bridge/WeComBridge.hpp>
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <fstream>
#include <cstdlib>

// WebSocket 支持 (使用 httplib 的 WebSocket 扩展)
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/ssl.h>
#endif

namespace claude::wecom {

// ============================================================================
// WebSocket 客户端 (简化实现)
// ============================================================================

class WebSocketClient {
public:
    using OnMessage = std::function<void(const String&)>;
    using OnOpen = std::function<void()>;
    using OnClose = std::function<void()>;
    using OnError = std::function<void(const String&)>;

    WebSocketClient() = default;
    ~WebSocketClient() { disconnect(); }

    bool connect(const String& url, const String& token) {
        // 解析 URL
        String host, path;
        int port = 443;
        bool useSSL = true;

        String u = url;
        if (u.substr(0, 6) == "wss://") {
            host = u.substr(6);
            useSSL = true;
            port = 443;
        } else if (u.substr(0, 5) == "ws://") {
            host = u.substr(5);
            useSSL = false;
            port = 80;
        }

        auto slashPos = host.find('/');
        if (slashPos != String::npos) {
            path = host.substr(slashPos);
            host = host.substr(0, slashPos);
        } else {
            path = "/";
        }

        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        // 构建完整路径
        path = path + "?access_token=" + token;

        spdlog::debug("[WS] Connecting to {}:{}{}", host, port, path);

        // 创建 socket 连接
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            spdlog::error("[WS] Failed to create socket");
            return false;
        }

        // 解析主机名
        struct hostent* he = gethostbyname(host.c_str());
        if (!he) {
            spdlog::error("[WS] Failed to resolve host: {}", host);
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        // 连接
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);

        if (::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            spdlog::error("[WS] Failed to connect");
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        // WebSocket 握手
        String request =
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + host + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";

        send(sockfd_, request.c_str(), request.size(), 0);

        // 读取响应
        char buf[1024];
        int n = recv(sockfd_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            spdlog::error("[WS] Failed to receive handshake response");
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        buf[n] = '\0';

        // 检查握手成功
        String response(buf);
        if (response.find("101") == String::npos ||
            response.find("Upgrade") == String::npos) {
            spdlog::error("[WS] WebSocket handshake failed: {}", response);
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        connected_ = true;
        spdlog::debug("[WS] Connected successfully");

        if (onOpen_) onOpen_();

        return true;
    }

    void disconnect() {
        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }
        connected_ = false;
        if (onClose_) onClose_();
    }

    bool isConnected() const { return connected_; }

    void setMessageHandler(OnMessage handler) { onMessage_ = std::move(handler); }
    void setOpenHandler(OnOpen handler) { onOpen_ = std::move(handler); }
    void setCloseHandler(OnClose handler) { onClose_ = std::move(handler); }
    void setErrorHandler(OnError handler) { onError_ = std::move(handler); }

    bool sendText(const String& message) {
        if (!connected_ || sockfd_ < 0) return false;

        // WebSocket 文本帧
        std::vector<uint8_t> frame;

        // FIN + opcode (text = 0x81)
        frame.push_back(0x81);

        // Payload length
        size_t len = message.size();
        if (len <= 125) {
            frame.push_back(static_cast<uint8_t>(len));
        } else if (len <= 65535) {
            frame.push_back(126);
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
            }
        }

        // Payload (不掩码)
        frame.insert(frame.end(), message.begin(), message.end());

        ssize_t sent = ::send(sockfd_, frame.data(), frame.size(), 0);
        return sent == static_cast<ssize_t>(frame.size());
    }

    std::optional<String> receive(int timeoutMs = 1000) {
        if (!connected_ || sockfd_ < 0) return std::nullopt;

        // 设置超时
        struct pollfd pfd;
        pfd.fd = sockfd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, timeoutMs);
        if (ret <= 0) return std::nullopt;

        // 读取帧头
        uint8_t header[2];
        int n = recv(sockfd_, header, 2, 0);
        if (n != 2) {
            disconnect();
            return std::nullopt;
        }

        bool fin = (header[0] & 0x80) != 0;
        int opcode = header[0] & 0x0F;
        bool masked = (header[1] & 0x80) != 0;
        uint64_t len = header[1] & 0x7F;

        // 读取扩展长度
        if (len == 126) {
            uint8_t ext[2];
            recv(sockfd_, ext, 2, 0);
            len = (ext[0] << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            recv(sockfd_, ext, 8, 0);
            len = 0;
            for (int i = 0; i < 8; i++) {
                len = (len << 8) | ext[i];
            }
        }

        // 读取掩码
        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked) {
            recv(sockfd_, mask, 4, 0);
        }

        // 读取 payload
        String payload(len, '\0');
        size_t received = 0;
        while (received < len) {
            n = recv(sockfd_, &payload[received], len - received, 0);
            if (n <= 0) {
                disconnect();
                return std::nullopt;
            }
            received += n;
        }

        // 解掩码
        if (masked) {
            for (size_t i = 0; i < len; i++) {
                payload[i] ^= mask[i % 4];
            }
        }

        // 处理 opcode
        if (opcode == 0x8) {  // Close
            disconnect();
            return std::nullopt;
        }

        if (opcode == 0x9) {  // Ping
            sendPong(payload);
            return std::nullopt;
        }

        if (opcode == 0x0 || opcode == 0x1 || opcode == 0x2) {  // Continuation/Text/Binary
            return payload;
        }

        return std::nullopt;
    }

private:
    int sockfd_ = -1;
    bool connected_ = false;
    OnMessage onMessage_;
    OnOpen onOpen_;
    OnClose onClose_;
    OnError onError_;

    void sendPong(const String& payload) {
        if (sockfd_ < 0) return;

        std::vector<uint8_t> frame;
        frame.push_back(0x8A);  // FIN + Pong

        size_t len = payload.size();
        if (len <= 125) {
            frame.push_back(static_cast<uint8_t>(len));
        } else if (len <= 65535) {
            frame.push_back(126);
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
        }

        frame.insert(frame.end(), payload.begin(), payload.end());
        ::send(sockfd_, frame.data(), frame.size(), 0);
    }
};

// ============================================================================
// WeComApiClient 实现
// ============================================================================

WeComApiClient::WeComApiClient(const WeComConfig& config) : config_(config) {}

WeComApiClient::~WeComApiClient() = default;

void WeComApiClient::debug(const String& msg) {
    if (config_.debugMode) {
        spdlog::debug("[WeCom] {}", msg);
        if (debugCallback_) {
            debugCallback_(msg);
        }
    }
}

String WeComApiClient::getAccessToken() {
    std::lock_guard<std::mutex> lock(tokenMutex_);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    if (!accessToken_.empty() && now < tokenExpireTime_ - 60) {
        return accessToken_;
    }

    debug("Getting access token from: " + config_.apiBaseUrl);

    httplib::Client client(config_.apiBaseUrl);
    client.set_connection_timeout(10, 0);

    auto res = client.Get(config_.endpoints.getAccessToken +
                          "?corpid=" + config_.corpId + "&corpsecret=" + config_.secret);
    if (!res) {
        spdlog::error("[WeCom] Failed to get access token: connection error");
        return "";
    }

    try {
        Json data = Json::parse(res->body);
        if (data.value("errcode", -1) == 0 && data.contains("access_token")) {
            accessToken_ = data["access_token"];
            tokenExpireTime_ = now + data.value("expires_in", 7200);
            spdlog::debug("[WeCom] Access token refreshed, expires in {}s",
                         tokenExpireTime_ - now);
            return accessToken_;
        }
        spdlog::error("[WeCom] Failed to get access token: {}", data.dump());
    } catch (...) {
        spdlog::error("[WeCom] Failed to parse token response");
    }

    return "";
}

bool WeComApiClient::refreshAccessToken() {
    accessToken_.clear();
    tokenExpireTime_ = 0;
    return !getAccessToken().empty();
}

std::pair<int, Json> WeComApiClient::httpGet(const String& path, const String& token) {
    httplib::Client client(config_.apiBaseUrl);
    client.set_connection_timeout(config_.messageTimeout, 0);

    httplib::Headers headers;
    if (!token.empty()) {
        headers.insert({"Authorization", "Bearer " + token});
    }

    auto res = client.Get(path, headers);
    if (!res) {
        return {0, Json::object()};
    }

    try {
        return {res->status, Json::parse(res->body)};
    } catch (...) {
        return {res->status, Json::object()};
    }
}

std::pair<int, Json> WeComApiClient::httpPost(const String& path, const Json& body,
                                               const String& token) {
    httplib::Client client(config_.apiBaseUrl);
    client.set_connection_timeout(config_.messageTimeout, 0);

    httplib::Headers headers = {{"Content-Type", "application/json"}};
    if (!token.empty()) {
        headers.insert({"Authorization", "Bearer " + token});
    }

    auto res = client.Post(path, headers, body.dump(), "application/json");
    if (!res) {
        return {0, Json::object()};
    }

    try {
        return {res->status, Json::parse(res->body)};
    } catch (...) {
        return {res->status, Json::object()};
    }
}

bool WeComApiClient::sendTextMessage(const String& userId, const String& content) {
    String token = getAccessToken();
    if (token.empty()) return false;

    String path = config_.endpoints.sendMessage + "?access_token=" + token;

    Json body = {
        {"touser", userId},
        {"msgtype", "text"},
        {"agentid", std::stoi(config_.agentId)},
        {"text", {{"content", content}}},
        {"safe", 0}
    };

    auto [status, data] = httpPost(path, body);
    return status == 200 && data.value("errcode", -1) == 0;
}

bool WeComApiClient::sendMarkdownMessage(const String& userId, const String& content) {
    String token = getAccessToken();
    if (token.empty()) return false;

    String path = config_.endpoints.sendMessage + "?access_token=" + token;

    Json body = {
        {"touser", userId},
        {"msgtype", "markdown"},
        {"agentid", std::stoi(config_.agentId)},
        {"markdown", {{"content", content}}}
    };

    auto [status, data] = httpPost(path, body);
    return status == 200 && data.value("errcode", -1) == 0;
}

bool WeComApiClient::sendImageMessage(const String& userId, const String& mediaId) {
    String token = getAccessToken();
    if (token.empty()) return false;

    String path = config_.endpoints.sendMessage + "?access_token=" + token;

    Json body = {
        {"touser", userId},
        {"msgtype", "image"},
        {"agentid", std::stoi(config_.agentId)},
        {"image", {{"media_id", mediaId}}}
    };

    auto [status, data] = httpPost(path, body);
    return status == 200 && data.value("errcode", -1) == 0;
}

bool WeComApiClient::sendChatTextMessage(const String& chatId, const String& content,
                                          const std::optional<String>& mentionUser) {
    String token = getAccessToken();
    if (token.empty()) return false;

    String path = config_.endpoints.sendChatMessage + "?access_token=" + token;

    Json body = {
        {"chatid", chatId},
        {"msgtype", "text"},
        {"text", {{"content", content}}}
    };

    if (mentionUser) {
        body["text"]["mentioned_list"] = Json::array({*mentionUser});
    }

    auto [status, data] = httpPost(path, body);
    return status == 200 && data.value("errcode", -1) == 0;
}

bool WeComApiClient::sendChatMarkdownMessage(const String& chatId, const String& content) {
    String token = getAccessToken();
    if (token.empty()) return false;

    String path = config_.endpoints.sendChatMessage + "?access_token=" + token;

    Json body = {
        {"chatid", chatId},
        {"msgtype", "markdown"},
        {"markdown", {{"content", content}}}
    };

    auto [status, data] = httpPost(path, body);
    return status == 200 && data.value("errcode", -1) == 0;
}

bool WeComApiClient::robotSendMessage(const String& chatId, const String& msgType,
                                       const Json& content) {
    String token = getAccessToken();
    if (token.empty()) return false;

    String path = config_.endpoints.robotSendMessage + "?access_token=" + token;

    Json body = {
        {"chatid", chatId},
        {"msgtype", msgType},
        {msgType, content}
    };

    auto [status, data] = httpPost(path, body);
    return status == 200 && data.value("errcode", -1) == 0;
}

Json WeComApiClient::getUserInfo(const String& userId) {
    String token = getAccessToken();
    if (token.empty()) return Json::object();

    String path = config_.endpoints.getUserInfo +
                  "?access_token=" + token + "&userid=" + userId;

    auto [status, data] = httpGet(path);
    return data;
}

Json WeComApiClient::getChatInfo(const String& chatId) {
    String token = getAccessToken();
    if (token.empty()) return Json::object();

    String path = config_.endpoints.getChatInfo +
                  "?access_token=" + token + "&chatid=" + chatId;

    auto [status, data] = httpGet(path);
    return data;
}

String WeComApiClient::uploadMedia(const String& filePath, const String& type) {
    String token = getAccessToken();
    if (token.empty()) return "";

    String path = config_.endpoints.uploadMedia +
                  "?access_token=" + token + "&type=" + type;

    // TODO: 实现 multipart 文件上传
    return "";
}

// ============================================================================
// WeComBridge 实现
// ============================================================================

WeComBridge::WeComBridge(const WeComConfig& config)
    : config_(config), apiClient_(std::make_unique<WeComApiClient>(config)) {}

WeComBridge::~WeComBridge() {
    stop();
}

bool WeComBridge::start() {
    if (running_) return true;

    running_ = true;
    workerThread_ = std::thread(&WeComBridge::workerLoop, this);

    spdlog::debug("[WeCom] Bridge started (mode: {})",
                 config_.connectionMode == ConnectionMode::Webhook ? "webhook" :
                 config_.connectionMode == ConnectionMode::LongPoll ? "longpoll" : "websocket");
    return true;
}

void WeComBridge::stop() {
    if (!running_) return;

    running_ = false;
    queueCv_.notify_all();

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    spdlog::debug("[WeCom] Bridge stopped");
}

void WeComBridge::workerLoop() {
    switch (config_.connectionMode) {
        case ConnectionMode::Webhook:
            workerLoopWebhook();
            break;
        case ConnectionMode::LongPoll:
            workerLoopLongPoll();
            break;
        case ConnectionMode::WebSocket:
            workerLoopWebSocket();
            break;
    }
}

void WeComBridge::workerLoopWebhook() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCv_.wait_for(lock, std::chrono::seconds(1), [this] {
            return !messageQueue_.empty() || !running_;
        });

        while (!messageQueue_.empty()) {
            auto msg = messageQueue_.front();
            messageQueue_.pop();
            lock.unlock();

            processMessage(msg);

            lock.lock();
        }
    }
}

void WeComBridge::workerLoopLongPoll() {
    spdlog::debug("[WeCom] Starting long poll mode");

    while (running_) {
        try {
            String token = apiClient_->getAccessToken();
            if (token.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            httplib::Client client(config_.apiBaseUrl);
            client.set_connection_timeout(config_.longPollTimeout + 5, 0);

            String path = config_.endpoints.longPollReceive +
                          "?access_token=" + token +
                          "&timeout=" + std::to_string(config_.longPollTimeout);

            auto res = client.Get(path);
            if (res && res->status == 200) {
                Json data = Json::parse(res->body);
                if (data.value("errcode", -1) == 0 && data.contains("messages")) {
                    for (const auto& msgData : data["messages"]) {
                        auto msg = parseMessage(msgData);
                        processMessage(msg);
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("[WeCom] Long poll error: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

void WeComBridge::workerLoopWebSocket() {
    spdlog::debug("[WeCom] Starting WebSocket mode");

    WebSocketClient ws;
    int reconnectDelay = config_.wsReconnectDelay;

    ws.setOpenHandler([this]() {
        spdlog::debug("[WeCom] WebSocket connected");
    });

    ws.setCloseHandler([this, &reconnectDelay]() {
        spdlog::warn("[WeCom] WebSocket disconnected, will reconnect in {}s", reconnectDelay);
    });

    ws.setMessageHandler([this](const String& data) {
        try {
            Json msgData = Json::parse(data);
            auto msg = parseMessage(msgData);
            processMessage(msg);
        } catch (const std::exception& e) {
            spdlog::error("[WeCom] Failed to parse WS message: {}", e.what());
        }
    });

    while (running_) {
        // 获取 token
        String token = apiClient_->getAccessToken();
        if (token.empty()) {
            spdlog::error("[WeCom] Failed to get token, retrying in {}s", reconnectDelay);
            std::this_thread::sleep_for(std::chrono::seconds(reconnectDelay));
            continue;
        }

        // 连接 WebSocket
        String wsUrl = config_.wsBaseUrl + config_.endpoints.wsConnect;

        if (ws.connect(wsUrl, token)) {
            reconnectDelay = config_.wsReconnectDelay;  // 重置重连延迟

            // 接收循环
            while (running_ && ws.isConnected()) {
                auto msg = ws.receive(1000);
                if (msg && ws.isConnected()) {
                    try {
                        Json msgData = Json::parse(*msg);
                        auto parsedMsg = parseMessage(msgData);
                        processMessage(parsedMsg);
                    } catch (...) {}
                }
            }
        }

        // 断开后等待重连
        ws.disconnect();
        if (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(reconnectDelay));
            // 指数退避
            reconnectDelay = std::min(reconnectDelay * 2, 60);
        }
    }
}

void WeComBridge::processMessage(const WeComMessage& msg) {
    auto response = handleMessage(msg);

    if (!response.content.empty()) {
        if (!msg.chatId.empty()) {
            apiClient_->sendChatTextMessage(msg.chatId, response.content,
                                            response.mentionUser);
        } else {
            apiClient_->sendTextMessage(msg.fromUser, response.content);
        }
    }
}

WeComResponse WeComBridge::handleMessage(const WeComMessage& msg) {
    switch (msg.type) {
        case WeComMessageType::Text:
            return handleTextMessage(msg);
        case WeComMessageType::Event:
            return handleEventMessage(msg);
        default:
            return {"text", "暂不支持此消息类型"};
    }
}

WeComResponse WeComBridge::handleTextMessage(const WeComMessage& msg) {
    auto& session = getOrCreateSession(msg.fromUser,
                                        msg.chatId.empty() ? std::nullopt :
                                        std::optional<String>(msg.chatId));

    String content = msg.content;

    if (content == "/clear" || content == "清空") {
        session.history.clear();
        return {"text", "会话已清空"};
    }

    if (content == "/help" || content == "帮助") {
        // 使用自定义帮助消息或默认消息
        if (!config_.helpMessage.empty()) {
            return {"text", config_.helpMessage};
        }
        String help = "🤖 " + config_.botName + " 助手\n\n"
                      "直接发送消息与我对话\n"
                      "/clear - 清空会话\n"
                      "/help - 显示帮助\n"
                      "/adminhelp - 配置管理";
        if (config_.showPoweredBy) {
            help += "\n\nPowered by Claude";
        }
        return {"text", help};
    }

    // 动态配置管理命令
    if (config_.allowDynamicConfig &&
        (content.substr(0, 4) == "/set" ||
         content == "/showconfig" || content == "/showConfig" ||
         content == "/saveconfig" || content == "/saveConfig" ||
         content == "/resetconfig" || content == "/resetConfig" ||
         content == "/geninstall" || content == "/genInstall" ||
         content == "/install" ||
         content == "/adminhelp")) {

        // 内联处理配置命令
        auto handleConfigCommand = [this](const String& userId, const String& msg) -> String {
            String cmd = msg;
            String args;
            auto spacePos = msg.find(' ');
            if (spacePos != String::npos) {
                cmd = msg.substr(0, spacePos);
                args = msg.substr(spacePos + 1);
            }

            // 显示管理帮助
            if (cmd == "/adminhelp") {
                return "🛠 配置管理命令\n\n"
                       "设置命令:\n"
                       "  /setName <名称>       - 设置机器人名称\n"
                       "  /setAlias <别名>      - 设置别名/启动命令名\n"
                       "  /setCliName <名称>    - 设置 CLI 启动命令\n"
                       "  /setWelcome <消息>    - 设置欢迎消息\n"
                       "  /setPoweredBy y/n     - 显示 'Powered by Claude'\n\n"
                       "管理命令:\n"
                       "  /showConfig           - 显示当前配置\n"
                       "  /saveConfig           - 保存配置\n"
                       "  /genInstall           - 生成安装脚本\n\n"
                       "示例:\n"
                       "  /setName 智能助手\n"
                       "  /setAlias mm\n"
                       "  /setCliName ee";
            }

            // 显示配置
            if (cmd == "/showconfig" || cmd == "/showConfig") {
                String result = "📋 当前配置\n\n";
                result += "机器人设置:\n";
                result += "  名称: " + config_.botName + "\n";
                result += "  别名: " + config_.botAlias + "\n";
                result += "  启动命令: " + config_.cliName + "\n";
                result += "  显示 'Powered by Claude': " + String(config_.showPoweredBy ? "是" : "否") + "\n\n";
                result += "连接设置:\n";
                result += "  API 地址: " + config_.apiBaseUrl + "\n";
                const char* modes[] = {"Webhook", "长轮询", "WebSocket"};
                result += "  连接模式: " + String(modes[static_cast<int>(config_.connectionMode)]) + "\n";
                return result;
            }

            // 设置名称
            if (cmd == "/setname" || cmd == "/setName") {
                if (args.empty()) return "用法: /setName <名称>";
                String old = config_.botName;
                config_.botName = args;
                return "✓ 机器人名称: " + old + " → " + args;
            }

            // 设置别名 (同时设置 CLI 名称)
            if (cmd == "/setalias" || cmd == "/setAlias") {
                if (args.empty()) return "用法: /setAlias <别名>";
                String old = config_.botAlias;
                config_.botAlias = args;
                config_.cliName = args;
                return "✓ 别名已设置: " + old + " → " + args +
                       "\n\n安装命令:\n  ./install.sh --name " + args;
            }

            // 设置 CLI 名称
            if (cmd == "/setcliname" || cmd == "/setCliName") {
                if (args.empty()) return "用法: /setCliName <名称>";
                String old = config_.cliName;
                config_.cliName = args;
                return "✓ CLI 命令名: " + old + " → " + args +
                       "\n\n安装:\n  make CLI_NAME=" + args + " install";
            }

            // 设置欢迎消息
            if (cmd == "/setwelcome" || cmd == "/setWelcome") {
                config_.welcomeMessage = args;
                return args.empty() ? "✓ 已恢复默认欢迎消息" : "✓ 欢迎消息已更新";
            }

            // 设置 Powered by
            if (cmd == "/setpoweredby" || cmd == "/setPoweredBy") {
                bool show = (args == "y" || args == "Y" || args == "true");
                config_.showPoweredBy = show;
                return String("✓ ") + (show ? "已显示" : "已隐藏") + " 'Powered by Claude'";
            }

            // 保存配置
            if (cmd == "/saveconfig" || cmd == "/saveConfig") {
                // 简单保存
                const char* home = std::getenv("HOME");
                if (home) {
                    String path = String(home) + "/.claude/wecom.json";
                    std::ofstream file(path);
                    if (file) {
                        Json j = {
                            {"corp_id", config_.corpId},
                            {"agent_id", config_.agentId},
                            {"secret", config_.secret},
                            {"api_base_url", config_.apiBaseUrl},
                            {"ws_base_url", config_.wsBaseUrl},
                            {"connection_mode", config_.connectionMode == ConnectionMode::Webhook ? "webhook" :
                                              config_.connectionMode == ConnectionMode::LongPoll ? "longpoll" : "websocket"},
                            {"bot_name", config_.botName},
                            {"bot_alias", config_.botAlias},
                            {"cli_name", config_.cliName},
                            {"show_powered_by", config_.showPoweredBy}
                        };
                        if (!config_.welcomeMessage.empty()) j["welcome_message"] = config_.welcomeMessage;
                        file << j.dump(2);
                        return "✓ 配置已保存到 ~/.claude/wecom.json";
                    }
                }
                return "✗ 保存失败";
            }

            // 生成安装脚本
            if (cmd == "/geninstall" || cmd == "/genInstall" || cmd == "/install") {
                String result = "📦 安装为 " + config_.cliName + ":\n\n";
                result += "./install.sh --name " + config_.cliName + "\n";
                result += "或\n";
                result += "make CLI_NAME=" + config_.cliName + " install\n\n";
                result += "安装后运行: " + config_.cliName;
                return result;
            }

            return "";
        };

        String configResponse = handleConfigCommand(msg.fromUser, content);
        if (!configResponse.empty()) {
            return {"text", configResponse};
        }
    }

    if (claudeHandler_) {
        try {
            String reply = claudeHandler_(session.sessionId, msg.fromUser,
                                          content, session.context);

            session.history.emplace_back("user", content);
            session.history.emplace_back("assistant", reply);
            session.lastActiveAt = std::chrono::system_clock::now();

            while (session.history.size() > static_cast<size_t>(config_.maxHistorySize * 2)) {
                session.history.erase(session.history.begin(),
                                      session.history.begin() + 2);
            }

            return {"text", reply};
        } catch (const std::exception& e) {
            spdlog::error("[WeCom] {} handler error: {}", config_.botAlias, e.what());
            String errorPrefix = config_.errorMessage.empty() ? "处理消息时出错" : config_.errorMessage;
            return {"text", errorPrefix + ": " + String(e.what())};
        }
    }

    return {"text", config_.botAlias + " 处理器未配置"};
}

WeComResponse WeComBridge::handleEventMessage(const WeComMessage& msg) {
    if (msg.eventType == "subscribe") {
        // 使用自定义欢迎消息或默认消息
        if (!config_.welcomeMessage.empty()) {
            return {"text", config_.welcomeMessage};
        }
        String welcome = "👋 欢迎使用 " + config_.botName + " 助手！\n\n"
                        "我是基于 " + config_.botAlias + " 的编程助手，可以帮你：\n"
                        "• 编写和调试代码\n"
                        "• 解答技术问题\n"
                        "• 执行各种开发任务\n\n"
                        "直接发送消息开始对话";
        if (config_.showPoweredBy) {
            welcome += "\n\nPowered by Claude";
        }
        return {"text", welcome};
    }

    if (msg.eventType == "unsubscribe") {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.erase(msg.fromUser);
    }

    return {"text", ""};
}

WeComSession& WeComBridge::getOrCreateSession(const String& userId,
                                               const std::optional<String>& chatId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);

    String sessionId = generateSessionId(userId, chatId);

    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        return it->second;
    }

    WeComSession session;
    session.sessionId = sessionId;
    session.userId = userId;
    session.chatId = chatId.value_or("");
    session.createdAt = std::chrono::system_clock::now();
    session.lastActiveAt = session.createdAt;

    sessions_[sessionId] = session;
    return sessions_[sessionId];
}

std::optional<WeComSession> WeComBridge::getSession(const String& sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void WeComBridge::cleanupExpiredSessions() {
    std::lock_guard<std::mutex> lock(sessionsMutex_);

    auto now = std::chrono::system_clock::now();
    auto maxDuration = std::chrono::seconds(config_.maxSessionDuration);

    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.lastActiveAt
        );
        if (elapsed > maxDuration) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

String WeComBridge::generateSessionId(const String& userId,
                                       const std::optional<String>& chatId) {
    if (chatId) {
        return "wecom-chat-" + *chatId;
    }
    return "wecom-user-" + userId;
}

WeComMessage WeComBridge::parseMessage(const Json& data) {
    WeComMessage msg;

    msg.msgId = data.value("MsgId", "");
    msg.fromUser = data.value("FromUserName", "");
    msg.toUser = data.value("ToUserName", "");
    msg.timestamp = data.value("CreateTime", 0);
    msg.raw = data;

    String msgType = data.value("MsgType", "");

    if (msgType == "text") {
        msg.type = WeComMessageType::Text;
        msg.content = data.value("Content", "");
    } else if (msgType == "image") {
        msg.type = WeComMessageType::Image;
        msg.content = data.value("PicUrl", "");
    } else if (msgType == "voice") {
        msg.type = WeComMessageType::Voice;
        msg.content = data.value("MediaId", "");
    } else if (msgType == "event") {
        msg.type = WeComMessageType::Event;
        msg.eventType = data.value("Event", "");
        msg.eventKey = data.value("EventKey", "");
    } else {
        msg.type = WeComMessageType::Unknown;
    }

    if (msg.fromUser.find("@chat") != String::npos) {
        msg.chatId = msg.fromUser;
        auto pos = msg.content.find("> ");
        if (pos != String::npos && msg.content.substr(0, 2) == "<@") {
            msg.fromUser = msg.content.substr(2, pos - 2);
            msg.content = msg.content.substr(pos + 2);
        }
    }

    return msg;
}

// ============================================================================
// WeComWebhookServer 实现
// ============================================================================

WeComWebhookServer::WeComWebhookServer(int port, WeComBridge& bridge)
    : port_(port), bridge_(bridge) {}

WeComWebhookServer::~WeComWebhookServer() {
    stop();
}

bool WeComWebhookServer::start() {
    if (running_) return true;

    running_ = true;
    serverThread_ = std::thread(&WeComWebhookServer::serve, this);

    spdlog::debug("[WeCom] Webhook server started on port {}", port_);
    return true;
}

void WeComWebhookServer::stop() {
    if (!running_) return;

    running_ = false;

    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void WeComWebhookServer::serve() {
    httplib::Server server;

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    server.Post("/callback", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            Json data = Json::parse(req.body);
            auto msg = bridge_.parseMessage(data);
            auto response = bridge_.handleMessage(msg);
            res.set_content(R"({"errcode":0,"errmsg":"ok"})", "application/json");
        } catch (const std::exception& e) {
            spdlog::error("[WeCom] Webhook error: {}", e.what());
            res.set_content(R"({"errcode":-1,"errmsg":"error"})", "application/json");
        }
    });

    server.listen("0.0.0.0", port_);
}

} // namespace claude::wecom
