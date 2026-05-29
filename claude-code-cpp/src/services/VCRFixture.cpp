#include <claude/services/VCRFixture.hpp>
#include <cstdlib>
#include <sstream>

namespace claude {

void VCRFixture::configureFromEnv() {
    const char* mode = std::getenv("CLAUDE_VCR_MODE");
    if (mode) {
        String modeStr(mode);
        if (modeStr == "record") {
            mode_ = Mode::Record;
            spdlog::debug("VCR: recording mode enabled");
        } else if (modeStr == "playback") {
            mode_ = Mode::Playback;
            spdlog::debug("VCR: playback mode enabled");
        } else {
            mode_ = Mode::Off;
        }
    }

    const char* cassette = std::getenv("CLAUDE_VCR_CASSETTE");
    if (cassette) {
        cassettePath_ = String(cassette);
        if (mode_ == Mode::Playback) {
            loadCassette(cassettePath_);
        }
        spdlog::debug("VCR: cassette path set to {}", cassettePath_);
    }
}

void VCRFixture::record(const Json& request, const Json& response) {
    if (mode_ != Mode::Record) return;

    std::lock_guard lock(mutex_);
    Interaction interaction;
    interaction.request = request;
    interaction.response = response;
    interaction.requestHash = computeHash(request);
    interactions_.push_back(std::move(interaction));

    // Auto-save after each recording
    if (!cassettePath_.empty()) {
        saveCassette(cassettePath_);
    }
}

std::optional<Json> VCRFixture::findPlayback(const Json& request) {
    if (mode_ != Mode::Playback) return std::nullopt;

    std::lock_guard lock(mutex_);
    String hash = computeHash(request);

    for (const auto& interaction : interactions_) {
        if (interaction.requestHash == hash) {
            playbackHits_++;
            return interaction.response;
        }
    }

    playbackMisses_++;
    spdlog::warn("VCR: no matching recording found for request (hash: {})", hash.substr(0, 8));
    return std::nullopt;
}

bool VCRFixture::saveCassette(const std::filesystem::path& path) const {
    std::lock_guard lock(mutex_);
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::trunc);
        for (const auto& interaction : interactions_) {
            Json entry = {
                {"request", interaction.request},
                {"response", interaction.response},
                {"hash", interaction.requestHash}
            };
            file << entry.dump() << "\n";
        }
        spdlog::debug("VCR: saved {} interactions to {}", interactions_.size(), path.string());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("VCR: failed to save cassette: {}", e.what());
        return false;
    }
}

bool VCRFixture::loadCassette(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    try {
        std::ifstream file(path);
        if (!file) {
            spdlog::warn("VCR: cassette file not found: {}", path.string());
            return false;
        }

        interactions_.clear();
        String line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            try {
                Json entry = Json::parse(line);
                Interaction interaction;
                interaction.request = entry.value("request", Json::object());
                interaction.response = entry.value("response", Json::object());
                interaction.requestHash = entry.value("hash", "");
                interactions_.push_back(std::move(interaction));
            } catch (const Json::parse_error&) {
                // Skip malformed lines
            }
        }
        spdlog::debug("VCR: loaded {} interactions from {}", interactions_.size(), path.string());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("VCR: failed to load cassette: {}", e.what());
        return false;
    }
}

void VCRFixture::clear() {
    std::lock_guard lock(mutex_);
    interactions_.clear();
    playbackHits_ = 0;
    playbackMisses_ = 0;
}

String VCRFixture::computeHash(const Json& request) {
    // Simple hash based on key fields
    String serialized = request.dump();
    // FNV-1a hash
    size_t hash = 2166136261u;
    for (char c : serialized) {
        hash ^= static_cast<size_t>(c);
        hash *= 16777619u;
    }
    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

} // namespace claude
