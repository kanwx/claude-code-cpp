#include <claude/mcp/McpTransport.hpp>
#include <httplib.h>
#include <sstream>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

namespace claude {

// ========== StdioTransport ==========

StdioTransport::StdioTransport(const String& command) {
    // 创建管道
    int stdinPipe[2];
    int stdoutPipe[2];

    if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) {
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return;
    }

    if (pid == 0) {
        // 子进程
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdinPipe[0]);
        close(stdoutPipe[1]);

        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    // 父进程
    close(stdinPipe[0]);
    close(stdoutPipe[1]);

    writePipe_ = fdopen(stdinPipe[1], "w");
    readPipe_ = fdopen(stdoutPipe[0], "r");
    connected_ = true;
}

StdioTransport::~StdioTransport() {
    if (writePipe_) fclose(writePipe_);
    if (readPipe_) fclose(readPipe_);
}

void StdioTransport::send(const String& message) {
    if (!connected_ || !writePipe_) return;

    fprintf(writePipe_, "%s\n", message.c_str());
    fflush(writePipe_);
}

String StdioTransport::receive() {
    if (!connected_ || !readPipe_) return "";

    std::array<char, 65536> buffer;
    if (fgets(buffer.data(), buffer.size(), readPipe_)) {
        String result = buffer.data();
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
        return result;
    }

    connected_ = false;
    return "";
}

bool StdioTransport::isConnected() const {
    return connected_;
}

// ========== SseTransport ==========

SseTransport::SseTransport(const String& url)
    : url_(url) {
    parseUrl(url);
}

SseTransport::~SseTransport() {
    running_ = false;
    inboxCv_.notify_all();
    if (listenThread_.joinable()) {
        listenThread_.join();
    }
}

void SseTransport::connect() {
    running_ = true;
    connected_ = true;
    listenThread_ = std::thread(&SseTransport::listenLoop, this);
}

void SseTransport::parseUrl(const String& url) {
    // Parse: http[s]://host[:port][/path]
    String remaining = url;

    bool useSSL = false;
    if (remaining.find("https://") == 0) {
        useSSL = true;
        remaining = remaining.substr(8);
    } else if (remaining.find("http://") == 0) {
        remaining = remaining.substr(7);
    }

    // Extract path
    auto slashPos = remaining.find('/');
    if (slashPos != String::npos) {
        basePath_ = remaining.substr(slashPos);
        remaining = remaining.substr(0, slashPos);
    } else {
        basePath_ = "/sse";
    }

    // Extract port
    auto colonPos = remaining.rfind(':');
    if (colonPos != String::npos) {
        port_ = std::stoi(remaining.substr(colonPos + 1));
        host_ = remaining.substr(0, colonPos);
    } else {
        port_ = useSSL ? 443 : 80;
        host_ = remaining;
    }

    // Default SSE path
    if (basePath_.empty() || basePath_ == "/") {
        basePath_ = "/sse";
    }
}

void SseTransport::send(const String& message) {
    if (!connected_) return;

    // Determine the POST endpoint for sending messages
    String postPath;
    {
        std::lock_guard<std::mutex> lock(messagesPathMutex_);
        postPath = messagesPath_.empty() ? "/messages" : messagesPath_;
    }

    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"}
    };

    try {
        httplib::Result res;
        if (port_ == 443) {
            httplib::SSLClient client(host_, port_);
            client.set_connection_timeout(10, 0);
            client.set_read_timeout(30, 0);
            res = client.Post(postPath, headers, message, "application/json");
        } else {
            httplib::Client client(host_, port_);
            client.set_connection_timeout(10, 0);
            client.set_read_timeout(30, 0);
            res = client.Post(postPath, headers, message, "application/json");
        }

        if (!res) {
            connected_ = false;
            return;
        }

        // The POST response may contain a single JSON-RPC response or
        // an SSE stream of responses. Handle both cases.
        const auto& contentType = res->get_header_value("Content-Type");
        if (contentType.find("text/event-stream") != String::npos) {
            // SSE response from POST — parse events
            std::istringstream stream(res->body);
            String line;
            String dataBuffer;
            while (std::getline(stream, line)) {
                // Remove trailing \r
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.find("data:") == 0) {
                    String data = line.substr(5);
                    while (!data.empty() && data.front() == ' ') {
                        data.erase(data.begin());
                    }
                    dataBuffer = data;
                } else if (line.empty() && !dataBuffer.empty()) {
                    // Empty line = end of event
                    std::lock_guard<std::mutex> lock(inboxMutex_);
                    inbox_.push_back(dataBuffer);
                    inboxCv_.notify_one();
                    dataBuffer.clear();
                }
            }
        } else if (!res->body.empty()) {
            // Single JSON response
            std::lock_guard<std::mutex> lock(inboxMutex_);
            inbox_.push_back(res->body);
            inboxCv_.notify_one();
        }
    } catch (const std::exception&) {
        connected_ = false;
    }
}

String SseTransport::receive() {
    std::unique_lock<std::mutex> lock(inboxMutex_);
    inboxCv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
        return !inbox_.empty() || !running_;
    });

    if (inbox_.empty()) return "";

    String msg = inbox_.front();
    inbox_.pop_front();
    return msg;
}

bool SseTransport::isConnected() const {
    return connected_;
}

void SseTransport::listenLoop() {
    httplib::Headers headers = {
        {"Accept", "text/event-stream"},
        {"Cache-Control", "no-cache"},
    };

    String sseBuffer;

    auto contentReceiver = [&](const char* data, size_t len) -> bool {
        if (!running_) return false;

        sseBuffer.append(data, len);

        // Process complete SSE frames in buffer
        // SSE format: "event: xxx\ndata: {...}\n\n"
        size_t pos = 0;
        while (pos < sseBuffer.size()) {
            // Find the next double-newline (end of event)
            size_t endOfEvent = sseBuffer.find("\n\n", pos);
            if (endOfEvent == String::npos) {
                // Also try \r\n\r\n
                size_t crlfEnd = sseBuffer.find("\r\n\r\n", pos);
                if (crlfEnd != String::npos) {
                    endOfEvent = crlfEnd + 2; // point to second \r\n
                } else {
                    break; // incomplete event, wait for more data
                }
            }

            // Extract the event block
            String eventBlock = sseBuffer.substr(pos, endOfEvent - pos);

            // Parse event lines
            std::istringstream eventStream(eventBlock);
            String line;
            String eventType;
            String dataValue;
            bool hasData = false;

            while (std::getline(eventStream, line)) {
                // Remove trailing \r
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (line.find("event:") == 0) {
                    eventType = line.substr(6);
                    while (!eventType.empty() && eventType.front() == ' ') {
                        eventType.erase(eventType.begin());
                    }
                } else if (line.find("data:") == 0) {
                    String d = line.substr(5);
                    while (!d.empty() && d.front() == ' ') {
                        d.erase(d.begin());
                    }
                    if (!dataValue.empty()) {
                        dataValue += "\n";
                    }
                    dataValue += d;
                    hasData = true;
                } else if (line.find(":") == 0) {
                    // Comment, ignore
                }
            }

            if (hasData) {
                if (eventType == "endpoint") {
                    // MCP spec: the "endpoint" event provides the POST path
                    // The data value may be a relative or absolute URL
                    std::lock_guard<std::mutex> lock(messagesPathMutex_);
                    if (dataValue.find("http") == 0) {
                        // Absolute URL — extract path portion
                        auto afterScheme = dataValue.find("://");
                        if (afterScheme != String::npos) {
                            String noScheme = dataValue.substr(afterScheme + 3);
                            auto slashAt = noScheme.find('/');
                            if (slashAt != String::npos) {
                                messagesPath_ = noScheme.substr(slashAt);
                            }
                        }
                    } else {
                        messagesPath_ = dataValue;
                    }
                } else if (eventType == "message" || eventType.empty()) {
                    // JSON-RPC message from server
                    std::lock_guard<std::mutex> lock(inboxMutex_);
                    inbox_.push_back(dataValue);
                    inboxCv_.notify_one();
                }
            }

            // Advance past the double-newline separator
            pos = endOfEvent + 2;
        }

        // Keep unprocessed portion in buffer
        if (pos > 0) {
            sseBuffer = sseBuffer.substr(pos);
        }

        return true;
    };

    auto responseHandler = [&](const httplib::Response& response) -> bool {
        return running_;
    };

    try {
        httplib::Result res;
        if (port_ == 443) {
            httplib::SSLClient client(host_, port_);
            client.set_connection_timeout(10, 0);
            client.set_read_timeout(300, 0);  // 5 min read timeout for SSE
            res = client.Get(basePath_, headers, responseHandler, contentReceiver);
        } else {
            httplib::Client client(host_, port_);
            client.set_connection_timeout(10, 0);
            client.set_read_timeout(300, 0);
            res = client.Get(basePath_, headers, responseHandler, contentReceiver);
        }

        if (!res) {
            connected_ = false;
        }
    } catch (const std::exception&) {
        connected_ = false;
    }
}

// ========== HttpTransport ==========

HttpTransport::HttpTransport(const String& url, int timeoutMs)
    : url_(url), timeoutMs_(timeoutMs) {
    parseUrl(url);
}

void HttpTransport::connect() {
    connected_ = true;
}

void HttpTransport::parseUrl(const String& url) {
    String remaining = url;

    bool useSSL = false;
    if (remaining.find("https://") == 0) {
        useSSL = true;
        remaining = remaining.substr(8);
    } else if (remaining.find("http://") == 0) {
        remaining = remaining.substr(7);
    }

    // Extract path
    auto slashPos = remaining.find('/');
    if (slashPos != String::npos) {
        basePath_ = remaining.substr(slashPos);
        remaining = remaining.substr(0, slashPos);
    } else {
        basePath_ = "/mcp";
    }

    // Extract port
    auto colonPos = remaining.rfind(':');
    if (colonPos != String::npos) {
        port_ = std::stoi(remaining.substr(colonPos + 1));
        host_ = remaining.substr(0, colonPos);
    } else {
        port_ = useSSL ? 443 : 80;
        host_ = remaining;
    }

    // Default MCP path
    if (basePath_.empty() || basePath_ == "/") {
        basePath_ = "/mcp";
    }
}

void HttpTransport::send(const String& message) {
    if (!connected_) return;

    // For notifications (no id field), fire-and-forget POST
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"}
    };

    try {
        httplib::Result res;
        if (port_ == 443) {
            httplib::SSLClient client(host_, port_);
            client.set_connection_timeout(10, 0);
            client.set_read_timeout(timeoutMs_ / 1000, (timeoutMs_ % 1000) * 1000);
            res = client.Post(basePath_, headers, message, "application/json");
        } else {
            httplib::Client client(host_, port_);
            client.set_connection_timeout(10, 0);
            client.set_read_timeout(timeoutMs_ / 1000, (timeoutMs_ % 1000) * 1000);
            res = client.Post(basePath_, headers, message, "application/json");
        }

        if (res) {
            const auto& contentType = res->get_header_value("Content-Type");
            if (contentType.find("text/event-stream") != String::npos) {
                // SSE response — parse events and queue them
                std::istringstream stream(res->body);
                String line;
                String dataBuffer;
                while (std::getline(stream, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (line.find("data:") == 0) {
                        String data = line.substr(5);
                        while (!data.empty() && data.front() == ' ') {
                            data.erase(data.begin());
                        }
                        dataBuffer = data;
                    } else if (line.empty() && !dataBuffer.empty()) {
                        std::lock_guard<std::mutex> lock(pendingMutex_);
                        pending_.push_back(dataBuffer);
                        dataBuffer.clear();
                    }
                }
                // Flush last event if no trailing newline
                if (!dataBuffer.empty()) {
                    std::lock_guard<std::mutex> lock(pendingMutex_);
                    pending_.push_back(dataBuffer);
                }
            } else if (!res->body.empty()) {
                // Single JSON response
                std::lock_guard<std::mutex> lock(pendingMutex_);
                pending_.push_back(res->body);
            }
        } else {
            connected_ = false;
        }
    } catch (const std::exception&) {
        connected_ = false;
    }
}

String HttpTransport::receive() {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    if (pending_.empty()) return "";

    String msg = pending_.front();
    pending_.pop_front();
    return msg;
}

bool HttpTransport::isConnected() const {
    return connected_;
}

} // namespace claude
