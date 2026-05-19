#pragma once


#include "../core/Types.hpp"

#include <string>
#include <functional>
#include <expected>
#include <nlohmann/json.hpp>

namespace claude {

using Json = nlohmann::json;

/// API 客户端基类
class ApiClient {
public:
    virtual ~ApiClient() = default;

    // ========== 配置 ==========

    virtual void setApiKey(const String& key) = 0;
    virtual void setBaseUrl(const String& url) = 0;
    virtual void setModel(const String& model) = 0;
    virtual void setMaxTokens(int maxTokens) = 0;
    virtual void setTemperature(double temp) = 0;

    // ========== 调用 ==========

    /// 阻塞调用
    virtual std::expected<Json, String> call(
        const Json& messages,
        const Json& tools
    ) = 0;

    /// 流式调用
    virtual void stream(
        const Json& messages,
        const Json& tools,
        std::function<void(const Json& chunk)> onChunk
    ) = 0;

    // ========== 中断 ==========

    /// Abort the in-flight streaming request
    virtual void abort() {}

    /// Check if an abort was requested
    virtual bool isAborted() const { return false; }

    /// Reset abort state for a new request
    virtual void resetAbort() {}

    // ========== 信息 ==========

    virtual String getProviderName() const = 0;
    virtual String getModelName() const = 0;
};

} // namespace claude
