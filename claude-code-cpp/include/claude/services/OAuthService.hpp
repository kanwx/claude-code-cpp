#pragma once

#include "../core/Types.hpp"
#include <functional>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace claude::oauth {

using Json = nlohmann::json;

/// OAuth 配置
struct OAuthConfig {
    String clientId;
    String clientSecret;
    String redirectUri;
    String authorizationUrl;
    String tokenUrl;
    String scope;
    /// Device authorization endpoint (for device code flow)
    String deviceAuthorizationUrl;
    int timeout = 300000;  // 5分钟
};

/// OAuth 令牌
struct OAuthToken {
    String accessToken;
    String refreshToken;
    String tokenType = "Bearer";
    int expiresIn = 3600;           // 秒
    std::chrono::system_clock::time_point issuedAt;
    String scope;

    /// 检查是否过期
    bool isExpired() const {
        auto now = std::chrono::system_clock::now();
        auto expires = issuedAt + std::chrono::seconds(expiresIn);
        return now >= expires;
    }

    /// 检查是否即将过期 (5分钟内)
    bool isExpiringSoon() const {
        auto now = std::chrono::system_clock::now();
        auto expires = issuedAt + std::chrono::seconds(expiresIn - 300);
        return now >= expires;
    }
};

/// PKCE challenge/response pair for public clients
struct PKCEChallenge {
    String verifier;    // Random base64url-encoded string (43 chars)
    String challenge;   // SHA256(verifier) base64url-encoded
    String method = "S256";

    /// Generate a new PKCE verifier/challenge pair
    static PKCEChallenge generate();
};

/// Device code flow response
struct DeviceCodeResponse {
    String deviceCode;
    String userCode;
    String verificationUri;
    String verificationUriComplete;  // Optional: URI with user code embedded
    int intervalSeconds = 5;
    int expiresIn = 900;
};

/// OAuth 客户端
class OAuthClient {
public:
    OAuthClient();
    ~OAuthClient();

    /// 初始化
    bool initialize(const OAuthConfig& config);

    /// 获取授权URL (optionally includes PKCE code_challenge)
    String getAuthorizationUrl(const String& state = "",
                               const PKCEChallenge* pkce = nullptr);

    /// 用授权码交换令牌 (optionally includes PKCE code_verifier)
    std::optional<OAuthToken> exchangeCodeForToken(const String& code,
                                                   const PKCEChallenge* pkce = nullptr);

    /// 刷新令牌
    std::optional<OAuthToken> refreshToken(const String& refreshToken);

    /// 获取当前令牌
    std::optional<OAuthToken> getCurrentToken() const;

    /// 设置令牌
    void setToken(const OAuthToken& token);

    /// 清除令牌
    void clearToken();

    /// 检查是否已认证
    bool isAuthenticated() const;

    /// 确保 valid token (自动刷新)
    bool ensureValidToken();

    /// 设置令牌更新回调
    void setTokenUpdateCallback(std::function<void(const OAuthToken&)> callback);

    // ========== Local Callback Server ==========

    /// Start a local HTTP server to capture the OAuth callback code.
    /// Blocks until the code is received or timeout expires.
    /// @param port The port to listen on (default 8787)
    /// @param timeoutSeconds Max wait time (default 120)
    /// @return The authorization code, or empty on timeout/error
    String waitForCallbackCode(int port = 8787, int timeoutSeconds = 120);

    // ========== Device Code Flow ==========

    /// Start device code flow for headless/CLI environments.
    /// @param providerName Name used for error messages only
    /// @return DeviceCodeResponse on success, error string on failure
    Result<DeviceCodeResponse> startDeviceFlow();

    /// Poll for device code completion. Returns access token on success.
    /// @param deviceCode The device code from startDeviceFlow
    /// @param intervalSeconds Polling interval (from DeviceCodeResponse)
    /// @return The access token string on success, error on failure
    Result<String> pollDeviceToken(const String& deviceCode, int intervalSeconds = 5);

    // ========== Convenience Login Methods ==========

    /// Full browser-based login with PKCE and local callback.
    /// Generates PKCE, opens browser, waits for callback, exchanges code.
    /// @return Access token on success, error on failure
    Result<String> loginWithBrowser();

    /// Full device code flow login for headless environments.
    /// Starts device flow, displays user code, polls until complete.
    /// @return Access token on success, error on failure
    Result<String> loginWithDeviceFlow();

private:
    OAuthConfig config_;
    std::optional<OAuthToken> token_;
    std::function<void(const OAuthToken&)> onTokenUpdate_;

    /// 保存令牌到文件
    bool saveToken(const OAuthToken& token);

    /// 从文件加载令牌
    std::optional<OAuthToken> loadToken();

    /// HTTP 请求
    Json httpRequest(const String& method, const String& url, const Json& data);

    /// URL-encode a string
    static String urlEncode(const String& str);
};

/// OAuth 提供者
namespace providers {

/// Anthropic OAuth
OAuthConfig anthropic();

/// Claude.ai OAuth
OAuthConfig claudeAi();

/// GitHub OAuth
OAuthConfig github();

/// 自定义 OAuth
OAuthConfig custom(const String& authUrl, const String& tokenUrl);

} // namespace providers

/// OAuth 管理器
class OAuthManager {
public:
    static OAuthManager& instance() {
        static OAuthManager manager;
        return manager;
    }

    /// 注册提供者
    void registerProvider(const String& name, const OAuthConfig& config);

    /// 获取客户端
    OAuthClient& getClient(const String& provider);

    /// 认证
    bool authenticate(const String& provider);

    /// 注销
    void logout(const String& provider);

    /// 刷新所有令牌
    void refreshAll();

    /// 检查认证状态
    bool isAuthenticated(const String& provider) const;

    /// Full browser-based login for a registered provider
    Result<String> loginWithBrowser(const String& provider);

    /// Full device code flow login for a registered provider
    Result<String> loginWithDeviceFlow(const String& provider);

private:
    OAuthManager() = default;

    std::unordered_map<String, OAuthConfig> providers_;
    std::unordered_map<String, std::unique_ptr<OAuthClient>> clients_;
};

} // namespace claude::oauth
