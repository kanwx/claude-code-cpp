#pragma once


#include "../core/Types.hpp"

#include <string>
#include <cstdio>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <future>

namespace claude {

/// MCP 传输层
class McpTransport {
public:
    virtual ~McpTransport() = default;

    /// 建立连接 (对于 Stdio 已在构造函数完成，对于 SSE/HTTP 需显式调用)
    virtual void connect() {}

    /// 发送消息
    virtual void send(const String& message) = 0;

    /// 接收消息
    virtual String receive() = 0;

    /// 是否连接
    virtual bool isConnected() const = 0;

    /// Asynchronous call: dispatches send+receive in a background thread.
    /// Returns a future that resolves with the response message.
    virtual std::future<String> asyncCall(const String& message) {
        return std::async(std::launch::async, [this, message]() {
            send(message);
            return receive();
        });
    }
};

/// StdIO 传输
class StdioTransport : public McpTransport {
public:
    StdioTransport(const String& command);
    ~StdioTransport();

    void send(const String& message) override;
    String receive() override;
    bool isConnected() const override;

private:
    FILE* readPipe_ = nullptr;
    FILE* writePipe_ = nullptr;
    bool connected_ = false;
};

/// SSE (Server-Sent Events) 传输
/// MCP SSE protocol: client POSTs JSON-RPC to a messages endpoint,
/// server pushes JSON-RPC responses via a persistent SSE stream.
class SseTransport : public McpTransport {
public:
    explicit SseTransport(const String& url);
    ~SseTransport();

    void connect() override;
    void send(const String& message) override;
    String receive() override;
    bool isConnected() const override;

private:
    /// Parse "http[s]://host:port/path" into host_, port_, basePath_
    void parseUrl(const String& url);

    /// Background thread: GET the SSE endpoint, parse events, push to inbox_
    void listenLoop();

    String url_;
    String host_;
    int port_ = 80;
    String basePath_;            // e.g. "/sse" or "/mcp/sse"
    String messagesPath_;        // discovered from SSE "endpoint" event
    std::mutex messagesPathMutex_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread listenThread_;

    /// Queue of messages received from the SSE stream
    std::mutex inboxMutex_;
    std::condition_variable inboxCv_;
    std::deque<String> inbox_;
};

/// HTTP (streamable) 传输
/// MCP streamable HTTP: each request is a POST; response may be
/// a single JSON-RPC object or an SSE stream of objects.
class HttpTransport : public McpTransport {
public:
    explicit HttpTransport(const String& url, int timeoutMs = 30000);

    void connect() override;
    void send(const String& message) override;
    String receive() override;
    bool isConnected() const override;

private:
    /// Parse "http[s]://host:port/path" into host_, port_, basePath_
    void parseUrl(const String& url);

    String url_;
    String host_;
    int port_ = 80;
    String basePath_;
    int timeoutMs_;

    std::atomic<bool> connected_{false};

    /// Pending responses from POST requests (for receive() to return)
    std::mutex pendingMutex_;
    std::deque<String> pending_;
};

} // namespace claude
