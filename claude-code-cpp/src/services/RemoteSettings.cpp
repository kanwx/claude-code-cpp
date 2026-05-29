#include <claude/services/RemoteSettings.hpp>
#include <claude/utils/Http.hpp>

namespace claude {

void RemoteSettings::start() {
    if (!enabled_ || endpoint_.empty()) {
        spdlog::debug("RemoteSettings: not enabled or no endpoint configured");
        return;
    }
    spdlog::debug("RemoteSettings: starting with endpoint {}", endpoint_);
    refresh();
}

void RemoteSettings::stop() {
    spdlog::debug("RemoteSettings: stopped");
    connected_ = false;
}

bool RemoteSettings::refresh() {
    if (endpoint_.empty()) return false;

    try {
        auto response = Http::get(endpoint_, {{"Accept", "application/json"}});
        if (!response) {
            spdlog::warn("RemoteSettings: failed to fetch from {}", endpoint_);
            connected_ = false;
            return false;
        }

        Json settings;
        try {
            settings = Json::parse(response->body);
        } catch (const Json::parse_error& e) {
            spdlog::warn("RemoteSettings: invalid JSON from endpoint: {}", e.what());
            connected_ = false;
            return false;
        }

        {
            std::lock_guard lock(mutex_);
            cachedSettings_ = settings;
            lastUpdated_ = std::chrono::system_clock::now();
            connected_ = true;
        }

        spdlog::debug("RemoteSettings: refreshed settings from remote");
        notifyCallbacks();
        return true;

    } catch (const std::exception& e) {
        spdlog::warn("RemoteSettings: exception during refresh: {}", e.what());
        connected_ = false;
        return false;
    }
}

Json RemoteSettings::getSettings() const {
    std::lock_guard lock(mutex_);
    return cachedSettings_;
}

bool RemoteSettings::getFeatureFlag(const String& name, bool defaultValue) const {
    std::lock_guard lock(mutex_);
    if (!cachedSettings_.contains("features")) return defaultValue;
    const auto& features = cachedSettings_["features"];
    if (!features.contains(name)) return defaultValue;
    return features[name].get<bool>();
}

void RemoteSettings::notifyCallbacks() {
    Json settingsCopy;
    {
        std::lock_guard lock(mutex_);
        settingsCopy = cachedSettings_;
    }
    for (const auto& cb : callbacks_) {
        try {
            cb(settingsCopy);
        } catch (const std::exception& e) {
            spdlog::warn("RemoteSettings: callback exception: {}", e.what());
        }
    }
}

} // namespace claude
