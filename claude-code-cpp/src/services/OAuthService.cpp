#include <claude/services/OAuthService.hpp>
#include <httplib.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <cstring>

namespace claude::oauth {

OAuthClient::OAuthClient() = default;
OAuthClient::~OAuthClient() = default;

bool OAuthClient::initialize(const OAuthConfig& config) {
    config_ = config;

    // 尝试加载已保存的令牌
    auto saved = loadToken();
    if (saved) {
        token_ = saved;
    }

    return true;
}


std::optional<OAuthToken> OAuthClient::refreshToken(const String& refreshToken) {
    Json data = {
        {"grant_type", "refresh_token"},
        {"refresh_token", refreshToken},
        {"client_id", config_.clientId},
        {"client_secret", config_.clientSecret}
    };

    Json response = httpRequest("POST", config_.tokenUrl, data);

    if (response.contains("error")) {
        return std::nullopt;
    }

    OAuthToken token;
    token.accessToken = response.value("access_token", "");
    token.refreshToken = response.value("refresh_token", refreshToken);
    token.tokenType = response.value("token_type", "Bearer");
    token.expiresIn = response.value("expires_in", 3600);
    token.scope = response.value("scope", config_.scope);
    token.issuedAt = std::chrono::system_clock::now();

    setToken(token);
    return token;
}

std::optional<OAuthToken> OAuthClient::getCurrentToken() const {
    return token_;
}

void OAuthClient::setToken(const OAuthToken& token) {
    token_ = token;
    saveToken(token);

    if (onTokenUpdate_) {
        onTokenUpdate_(token);
    }
}

void OAuthClient::clearToken() {
    token_ = std::nullopt;
    // 删除保存的令牌文件
}

bool OAuthClient::isAuthenticated() const {
    return token_.has_value() && !token_->isExpired();
}

bool OAuthClient::ensureValidToken() {
    if (!token_) return false;

    if (token_->isExpiringSoon() && !token_->refreshToken.empty()) {
        auto newToken = refreshToken(token_->refreshToken);
        return newToken.has_value();
    }

    return !token_->isExpired();
}

void OAuthClient::setTokenUpdateCallback(std::function<void(const OAuthToken&)> callback) {
    onTokenUpdate_ = std::move(callback);
}

// ========== PKCE ==========

PKCEChallenge PKCEChallenge::generate() {
    // Generate 32 random bytes
    unsigned char buf[32];
    if (RAND_bytes(buf, sizeof(buf)) != 1) {
        // Fallback: use /dev/urandom
        std::ifstream urandom("/dev/urandom", std::ios::binary);
        urandom.read(reinterpret_cast<char*>(buf), sizeof(buf));
        if (!urandom || urandom.gcount() != static_cast<std::streamsize>(sizeof(buf))) {
            // Last resort: use std::random_device
            std::random_device rd;
            for (size_t i = 0; i < sizeof(buf); i += sizeof(unsigned int)) {
                unsigned int val = rd();
                std::memcpy(buf + i, &val, std::min(sizeof(unsigned int), sizeof(buf) - i));
            }
        }
    }

    // Base64url-encode the random bytes to produce the verifier (43 chars)
    auto base64urlEncode = [](const unsigned char* data, size_t len) -> String {
        static const char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        String out;
        out.reserve((len * 4 + 2) / 3);
        for (size_t i = 0; i < len; i += 3) {
            unsigned int n = static_cast<unsigned int>(data[i]) << 16;
            if (i + 1 < len) n |= static_cast<unsigned int>(data[i + 1]) << 8;
            if (i + 2 < len) n |= static_cast<unsigned int>(data[i + 2]);
            out.push_back(kTable[(n >> 18) & 0x3F]);
            out.push_back(kTable[(n >> 12) & 0x3F]);
            out.push_back((i + 1 < len) ? kTable[(n >> 6) & 0x3F] : '\0');
            out.push_back((i + 2 < len) ? kTable[n & 0x3F] : '\0');
        }
        // Remove padding characters (base64url does not use '=')
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return out;
    };

    PKCEChallenge pkce;
    pkce.verifier = base64urlEncode(buf, sizeof(buf));

    // SHA256 hash the verifier
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(pkce.verifier.data()),
           pkce.verifier.size(), hash);

    // Base64url-encode the hash to produce the challenge
    pkce.challenge = base64urlEncode(hash, sizeof(hash));
    pkce.method = "S256";

    return pkce;
}

// ========== URL Encoding ==========

String OAuthClient::urlEncode(const String& str) {
    String result;
    result.reserve(str.size() * 3);
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            result += hex;
        }
    }
    return result;
}

// ========== Updated getAuthorizationUrl with PKCE ==========

String OAuthClient::getAuthorizationUrl(const String& state, const PKCEChallenge* pkce) {
    std::ostringstream oss;
    oss << config_.authorizationUrl;
    oss << "?response_type=code";
    oss << "&client_id=" << urlEncode(config_.clientId);
    oss << "&redirect_uri=" << urlEncode(config_.redirectUri);
    oss << "&scope=" << urlEncode(config_.scope);

    if (!state.empty()) {
        oss << "&state=" << urlEncode(state);
    }

    if (pkce) {
        oss << "&code_challenge=" << urlEncode(pkce->challenge);
        oss << "&code_challenge_method=" << pkce->method;
    }

    return oss.str();
}

// ========== Updated exchangeCodeForToken with PKCE ==========

std::optional<OAuthToken> OAuthClient::exchangeCodeForToken(const String& code,
                                                            const PKCEChallenge* pkce) {
    Json data = {
        {"grant_type", "authorization_code"},
        {"code", code},
        {"client_id", config_.clientId},
        {"client_secret", config_.clientSecret},
        {"redirect_uri", config_.redirectUri}
    };

    if (pkce) {
        data["code_verifier"] = pkce->verifier;
    }

    Json response = httpRequest("POST", config_.tokenUrl, data);

    if (response.contains("error")) {
        return std::nullopt;
    }

    OAuthToken token;
    token.accessToken = response.value("access_token", "");
    token.refreshToken = response.value("refresh_token", "");
    token.tokenType = response.value("token_type", "Bearer");
    token.expiresIn = response.value("expires_in", 3600);
    token.scope = response.value("scope", config_.scope);
    token.issuedAt = std::chrono::system_clock::now();

    setToken(token);
    return token;
}

// ========== Local Callback Server ==========

String OAuthClient::waitForCallbackCode(int port, int timeoutSeconds) {
    String authCode;
    std::atomic<bool> received{false};
    std::atomic<bool> timedOut{false};

    httplib::Server svr;

    svr.Get("/callback", [&](const httplib::Request& req, httplib::Response& res) {
        // Extract the code parameter
        if (req.has_param("code")) {
            authCode = req.get_param_value("code");
            received.store(true);

            // Return a nice page to the browser
            res.set_content(
                "<html><body style='font-family:sans-serif;text-align:center;padding-top:10%'>"
                "<h2>Authentication successful!</h2>"
                "<p>You can close this tab and return to the terminal.</p>"
                "</body></html>",
                "text/html");
        } else if (req.has_param("error")) {
            authCode.clear();
            received.store(true);
            String error = req.get_param_value("error");
            String errorDesc = req.has_param("error_description")
                                   ? req.get_param_value("error_description")
                                   : error;
            res.set_content(
                "<html><body style='font-family:sans-serif;text-align:center;padding-top:10%'>"
                "<h2>Authentication failed</h2>"
                "<p>" + errorDesc + "</p>"
                "</body></html>",
                "text/html");
        } else {
            res.set_content("Missing code parameter", "text/plain");
        }
    });

    // Start the server in a separate thread
    std::thread serverThread([&]() {
        if (!svr.listen("127.0.0.1", port)) {
            timedOut.store(true);
        }
    });

    // Wait for callback or timeout
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeoutSeconds);
    while (!received.load() && !timedOut.load()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            svr.stop();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    svr.stop();
    if (serverThread.joinable()) {
        serverThread.join();
    }

    return authCode;
}

// ========== Device Code Flow ==========

Result<DeviceCodeResponse> OAuthClient::startDeviceFlow() {
    if (config_.deviceAuthorizationUrl.empty()) {
        return Result<DeviceCodeResponse>::err(
            "No device authorization endpoint configured for this provider");
    }

    Json data = {
        {"client_id", config_.clientId},
        {"scope", config_.scope}
    };

    Json response = httpRequest("POST", config_.deviceAuthorizationUrl, data);

    if (response.contains("error")) {
        return Result<DeviceCodeResponse>::err(
            response.value("error_description",
                           response.value("error", "Device flow request failed")));
    }

    DeviceCodeResponse dcr;
    dcr.deviceCode = response.value("device_code", "");
    dcr.userCode = response.value("user_code", "");
    dcr.verificationUri = response.value("verification_uri", "");
    dcr.verificationUriComplete = response.value("verification_uri_complete", "");
    dcr.intervalSeconds = response.value("interval", 5);
    dcr.expiresIn = response.value("expires_in", 900);

    if (dcr.deviceCode.empty() || dcr.userCode.empty() || dcr.verificationUri.empty()) {
        return Result<DeviceCodeResponse>::err(
            "Incomplete device code response from server");
    }

    return Result<DeviceCodeResponse>::success(std::move(dcr));
}

Result<String> OAuthClient::pollDeviceToken(const String& deviceCode, int intervalSeconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);

    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));

        Json data = {
            {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
            {"device_code", deviceCode},
            {"client_id", config_.clientId}
        };

        Json response = httpRequest("POST", config_.tokenUrl, data);

        if (response.contains("error")) {
            String error = response.value("error", "");
            if (error == "authorization_pending") {
                // User hasn't completed the flow yet, keep polling
                continue;
            } else if (error == "slow_down") {
                // Increase polling interval by 5 seconds
                intervalSeconds += 5;
                continue;
            } else if (error == "expired_token") {
                return Result<String>::err("Device code expired. Please try again.");
            } else if (error == "access_denied") {
                return Result<String>::err("Authorization was denied by the user.");
            } else {
                return Result<String>::err(
                    response.value("error_description", error));
            }
        }

        // Success — we have a token
        String accessToken = response.value("access_token", "");
        if (accessToken.empty()) {
            return Result<String>::err("Token response missing access_token");
        }

        // Save the full token
        OAuthToken token;
        token.accessToken = accessToken;
        token.refreshToken = response.value("refresh_token", "");
        token.tokenType = response.value("token_type", "Bearer");
        token.expiresIn = response.value("expires_in", 3600);
        token.scope = response.value("scope", config_.scope);
        token.issuedAt = std::chrono::system_clock::now();
        setToken(token);

        return Result<String>::success(std::move(accessToken));
    }

    return Result<String>::err("Device code flow timed out after 15 minutes");
}

// ========== Convenience Login Methods ==========

Result<String> OAuthClient::loginWithBrowser() {
    // Generate PKCE for security
    auto pkce = PKCEChallenge::generate();

    // Generate state for CSRF protection
    unsigned char stateBuf[16];
    if (RAND_bytes(stateBuf, sizeof(stateBuf)) != 1) {
        std::ifstream urandom("/dev/urandom", std::ios::binary);
        urandom.read(reinterpret_cast<char*>(stateBuf), sizeof(stateBuf));
    }
    auto base64urlHex = [](const unsigned char* data, size_t len) -> String {
        static const char kHex[] = "0123456789abcdef";
        String out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; ++i) {
            out.push_back(kHex[(data[i] >> 4) & 0x0F]);
            out.push_back(kHex[data[i] & 0x0F]);
        }
        return out;
    };
    String state = base64urlHex(stateBuf, sizeof(stateBuf));

    // Build authorization URL with PKCE
    String authUrl = getAuthorizationUrl(state, &pkce);

    // Determine callback port from redirect URI
    int port = 8787;
    if (config_.redirectUri.find("localhost:") != String::npos) {
        auto colonPos = config_.redirectUri.find("localhost:") + 10;
        auto slashPos = config_.redirectUri.find('/', colonPos);
        String portStr = (slashPos != String::npos)
                             ? config_.redirectUri.substr(colonPos, slashPos - colonPos)
                             : config_.redirectUri.substr(colonPos);
        try { port = std::stoi(portStr); } catch (...) { port = 8787; }
    }

    // Open the browser
    String openCmd;
#ifdef __APPLE__
    openCmd = "open";
#elif defined(__linux__)
    openCmd = "xdg-open";
#else
    openCmd = "start";
#endif
    String cmd = openCmd + " \"" + authUrl + "\" 2>/dev/null";
    std::system(cmd.c_str());

    // Wait for the callback
    String code = waitForCallbackCode(port, 120);
    if (code.empty()) {
        return Result<String>::err(
            "No authorization code received (timeout or user cancelled)");
    }

    // Exchange code for token (with PKCE verifier)
    auto token = exchangeCodeForToken(code, &pkce);
    if (!token) {
        return Result<String>::err("Failed to exchange authorization code for token");
    }

    return Result<String>::success(token->accessToken);
}

Result<String> OAuthClient::loginWithDeviceFlow() {
    // Start device flow
    auto flowResult = startDeviceFlow();
    if (flowResult.isErr()) {
        return Result<String>::err(flowResult.error());
    }

    auto& dcr = flowResult.value();

    // Display instructions to the user
    std::cout << "\n=== Device Code Login ===\n"
              << "To authenticate, visit:\n\n"
              << "  " << dcr.verificationUri << "\n\n"
              << "And enter code: " << dcr.userCode << "\n\n"
              << "Waiting for authorization...\n" << std::flush;

    // Poll until complete
    auto tokenResult = pollDeviceToken(dcr.deviceCode, dcr.intervalSeconds);
    if (tokenResult.isErr()) {
        std::cout << "Device code flow failed: " << tokenResult.error() << "\n";
        return tokenResult;
    }

    std::cout << "Authentication successful!\n" << std::flush;
    return tokenResult;
}

bool OAuthClient::saveToken(const OAuthToken& token) {
    // 保存到 ~/.claude/oauth.json
    const char* home = std::getenv("HOME");
    if (!home) return false;

    std::filesystem::path dir = String(home) + "/.claude";
    std::filesystem::create_directories(dir);

    Json j = {
        {"access_token", token.accessToken},
        {"refresh_token", token.refreshToken},
        {"token_type", token.tokenType},
        {"expires_in", token.expiresIn},
        {"scope", token.scope},
        {"issued_at", std::chrono::duration_cast<std::chrono::seconds>(
            token.issuedAt.time_since_epoch()).count()}
    };

    std::ofstream file(dir / "oauth.json");
    if (!file) return false;

    file << j.dump(2);
    return true;
}

std::optional<OAuthToken> OAuthClient::loadToken() {
    const char* home = std::getenv("HOME");
    if (!home) return std::nullopt;

    std::filesystem::path path = String(home) + "/.claude/oauth.json";

    std::ifstream file(path);
    if (!file) return std::nullopt;

    std::stringstream buffer;
    buffer << file.rdbuf();

    try {
        Json j = Json::parse(buffer.str());

        OAuthToken token;
        token.accessToken = j.value("access_token", "");
        token.refreshToken = j.value("refresh_token", "");
        token.tokenType = j.value("token_type", "Bearer");
        token.expiresIn = j.value("expires_in", 3600);
        token.scope = j.value("scope", "");

        auto issued = j.value("issued_at", 0);
        token.issuedAt = std::chrono::system_clock::time_point(
            std::chrono::seconds(issued));

        return token;
    } catch (...) {
        return std::nullopt;
    }
}

Json OAuthClient::httpRequest(const String& method, const String& url, const Json& data) {
    // 解析URL
    String host, path;
    int port = 443;
    bool useSSL = true;

    if (url.substr(0, 8) == "https://") {
        host = url.substr(8);
        useSSL = true;
        port = 443;
    } else if (url.substr(0, 7) == "http://") {
        host = url.substr(7);
        useSSL = false;
        port = 80;
    } else {
        return {{"error", "Invalid URL"}};
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

    // 使用httplib发送请求
    try {
        if (useSSL) {
            httplib::SSLClient client(host, port);
            client.set_follow_location(true);
            client.set_connection_timeout(30, 0);

            httplib::Headers headers = {
                {"Content-Type", "application/json"},
                {"Accept", "application/json"}
            };

            String body = data.empty() ? "" : data.dump();

            httplib::Result result;
            if (method == "POST") {
                result = client.Post(path, headers, body, "application/json");
            } else if (method == "GET") {
                result = client.Get(path, headers);
            } else if (method == "PUT") {
                result = client.Put(path, headers, body, "application/json");
            } else if (method == "DELETE") {
                result = client.Delete(path, headers);
            } else {
                return {{"error", "Unsupported method"}};
            }

            if (result) {
                try {
                    return Json::parse(result->body);
                } catch (...) {
                    return {{"error", "Invalid JSON response"}, {"body", result->body}};
                }
            } else {
                return {{"error", httplib::to_string(result.error())}};
            }
        } else {
            httplib::Client client(host, port);
            client.set_follow_location(true);

            httplib::Headers headers = {
                {"Content-Type", "application/json"},
                {"Accept", "application/json"}
            };

            String body = data.empty() ? "" : data.dump();

            httplib::Result result;
            if (method == "POST") {
                result = client.Post(path, headers, body, "application/json");
            } else if (method == "GET") {
                result = client.Get(path, headers);
            } else {
                return {{"error", "Unsupported method"}};
            }

            if (result) {
                try {
                    return Json::parse(result->body);
                } catch (...) {
                    return {{"error", "Invalid JSON response"}, {"body", result->body}};
                }
            } else {
                return {{"error", httplib::to_string(result.error())}};
            }
        }
    } catch (const std::exception& e) {
        return {{"error", e.what()}};
    }
}

// ========== Providers ==========

namespace providers {

OAuthConfig anthropic() {
    return {
        .clientId = "claude-code",
        .clientSecret = "",
        .redirectUri = "http://localhost:8787/callback",
        .authorizationUrl = "https://claude.ai/oauth/authorize",
        .tokenUrl = "https://claude.ai/oauth/token",
        .scope = "openid profile email",
        .deviceAuthorizationUrl = "https://claude.ai/oauth/device_authorize"
    };
}

OAuthConfig claudeAi() {
    return anthropic();
}

OAuthConfig github() {
    return {
        .clientId = "",
        .clientSecret = "",
        .redirectUri = "http://localhost:8787/callback",
        .authorizationUrl = "https://github.com/login/oauth/authorize",
        .tokenUrl = "https://github.com/login/oauth/access_token",
        .scope = "user repo",
        .deviceAuthorizationUrl = "https://github.com/login/device/code"
    };
}

OAuthConfig custom(const String& authUrl, const String& tokenUrl) {
    OAuthConfig config;
    config.authorizationUrl = authUrl;
    config.tokenUrl = tokenUrl;
    return config;
}

} // namespace providers

// ========== OAuthManager ==========

void OAuthManager::registerProvider(const String& name, const OAuthConfig& config) {
    providers_[name] = config;
    clients_[name] = std::make_unique<OAuthClient>();
    clients_[name]->initialize(config);
}

OAuthClient& OAuthManager::getClient(const String& provider) {
    auto it = clients_.find(provider);
    if (it == clients_.end()) {
        // 创建默认客户端
        clients_[provider] = std::make_unique<OAuthClient>();
    }
    return *clients_[provider];
}

bool OAuthManager::authenticate(const String& provider) {
    auto it = clients_.find(provider);
    if (it == clients_.end()) return false;

    return it->second->isAuthenticated();
}

void OAuthManager::logout(const String& provider) {
    auto it = clients_.find(provider);
    if (it != clients_.end()) {
        it->second->clearToken();
    }
}

void OAuthManager::refreshAll() {
    for (auto& [name, client] : clients_) {
        client->ensureValidToken();
    }
}

bool OAuthManager::isAuthenticated(const String& provider) const {
    auto it = clients_.find(provider);
    return it != clients_.end() && it->second->isAuthenticated();
}

Result<String> OAuthManager::loginWithBrowser(const String& provider) {
    auto it = clients_.find(provider);
    if (it == clients_.end()) {
        return Result<String>::err("Provider not registered: " + provider);
    }
    return it->second->loginWithBrowser();
}

Result<String> OAuthManager::loginWithDeviceFlow(const String& provider) {
    auto it = clients_.find(provider);
    if (it == clients_.end()) {
        return Result<String>::err("Provider not registered: " + provider);
    }
    return it->second->loginWithDeviceFlow();
}

} // namespace claude::oauth
