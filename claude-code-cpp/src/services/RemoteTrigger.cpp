#include <claude/services/RemoteTrigger.hpp>
#include <spdlog/spdlog.h>

namespace claude {

bool RemoteTrigger::start() {
    if (listening_) {
        spdlog::warn("RemoteTrigger: already listening");
        return true;
    }

    if (port_ == 0) {
        spdlog::error("RemoteTrigger: no port configured");
        return false;
    }

    // TODO: Real HTTP server implementation would go here.
    // For now, mark as listening for testing purposes.
    listening_ = true;
    spdlog::debug("RemoteTrigger: listening on port {}", port_);
    return true;
}

void RemoteTrigger::stop() {
    if (!listening_) return;

    listening_ = false;
    spdlog::debug("RemoteTrigger: stopped listening");
}

void RemoteTrigger::trigger(const String& source, const String& content) {
    spdlog::debug("RemoteTrigger: triggered by '{}', content length={}", source, content.size());

    for (const auto& callback : callbacks_) {
        try {
            callback(source, content);
        } catch (const std::exception& e) {
            spdlog::error("RemoteTrigger: callback error: {}", e.what());
        }
    }
}

} // namespace claude
