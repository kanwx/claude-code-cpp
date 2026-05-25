#pragma once

#include "../core/Types.hpp"
#include <unordered_map>
#include <string>
#include <optional>

namespace claude {

/// Model alias types
enum class ModelAlias {
    Opus,
    Sonnet,
    Haiku,
    OpusPlan,
    Best,
    None  // Not an alias
};

/// Model family types
enum class ModelFamily {
    Opus,
    Sonnet,
    Haiku,
    Unknown
};

/// Model version info
struct ModelVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    String toString() const {
        if (patch > 0) {
            return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        }
        return std::to_string(major) + "." + std::to_string(minor);
    }
};

/// Model information for a specific model version
struct ModelInfo {
    String id;              // Full model ID (e.g., "claude-opus-4-6")
    String displayName;     // Display name (e.g., "Opus 4.6")
    String description;     // Description
    ModelFamily family = ModelFamily::Unknown;
    ModelVersion version;
    int maxContext = 200000;    // Default context window
    bool supports1M = false;    // Supports 1M context
    bool supportsThinking = false;
    double inputCostPer1M = 0.0;   // Input cost per 1M tokens
    double outputCostPer1M = 0.0;  // Output cost per 1M tokens
};

/// Model registry - manages all model configurations
class ModelRegistry {
public:
    static ModelRegistry& instance() {
        static ModelRegistry registry;
        return registry;
    }

    /// Parse model alias from string
    static ModelAlias parseAlias(const String& model) {
        String lower = model;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Strip [1m] suffix for alias check
        if (lower.ends_with("[1m]")) {
            lower = lower.substr(0, lower.length() - 4);
        }

        if (lower == "opus") return ModelAlias::Opus;
        if (lower == "sonnet") return ModelAlias::Sonnet;
        if (lower == "haiku") return ModelAlias::Haiku;
        if (lower == "opusplan") return ModelAlias::OpusPlan;
        if (lower == "best") return ModelAlias::Best;

        return ModelAlias::None;
    }

    /// Check if string is a model alias
    static bool isAlias(const String& model) {
        return parseAlias(model) != ModelAlias::None;
    }

    /// Check if model has [1m] suffix
    static bool has1MContext(const String& model) {
        String lower = model;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower.ends_with("[1m]");
    }

    /// Strip [1m] suffix from model string
    static String strip1MSuffix(const String& model) {
        if (has1MContext(model)) {
            return model.substr(0, model.length() - 4);
        }
        return model;
    }

    /// Get default Opus model
    String getDefaultOpusModel() const {
        const char* env = std::getenv("ANTHROPIC_DEFAULT_OPUS_MODEL");
        if (env) return env;
        return "claude-opus-4-7";
    }

    /// Get default Sonnet model
    String getDefaultSonnetModel() const {
        const char* env = std::getenv("ANTHROPIC_DEFAULT_SONNET_MODEL");
        if (env) return env;
        return "claude-sonnet-4-6";
    }

    /// Get default Haiku model
    String getDefaultHaikuModel() const {
        const char* env = std::getenv("ANTHROPIC_DEFAULT_HAIKU_MODEL");
        if (env) return env;
        return "claude-haiku-4-5-20251001";
    }

    /// Get small fast model (for quick tasks)
    String getSmallFastModel() const {
        const char* env = std::getenv("ANTHROPIC_SMALL_FAST_MODEL");
        if (env) return env;
        return getDefaultHaikuModel();
    }

    /// Get best model (most capable)
    String getBestModel() const {
        return getDefaultOpusModel();
    }

    /// Parse user-specified model (resolves aliases)
    String parseUserSpecifiedModel(const String& modelInput) const {
        String trimmed = modelInput;
        // Trim whitespace
        size_t start = trimmed.find_first_not_of(" \t\n\r");
        size_t end = trimmed.find_last_not_of(" \t\n\r");
        if (start != String::npos && end != String::npos) {
            trimmed = trimmed.substr(start, end - start + 1);
        }

        bool has1m = has1MContext(trimmed);
        String baseModel = strip1MSuffix(trimmed);
        std::transform(baseModel.begin(), baseModel.end(), baseModel.begin(), ::tolower);

        // Resolve aliases
        ModelAlias alias = parseAlias(baseModel);
        String resolved;

        switch (alias) {
            case ModelAlias::Opus:
                resolved = getDefaultOpusModel();
                break;
            case ModelAlias::Sonnet:
                resolved = getDefaultSonnetModel();
                break;
            case ModelAlias::Haiku:
                resolved = getDefaultHaikuModel();
                break;
            case ModelAlias::OpusPlan:
                // OpusPlan: Sonnet by default, Opus in plan mode
                resolved = getDefaultSonnetModel();
                break;
            case ModelAlias::Best:
                resolved = getBestModel();
                break;
            default:
                // Not an alias, use as-is (preserve original case for custom models)
                resolved = strip1MSuffix(modelInput);
                break;
        }

        // Add [1m] suffix if specified and model supports it
        if (has1m && supports1MContext(resolved)) {
            resolved += "[1m]";
        }

        return resolved;
    }

    /// Get model config by ID
    std::optional<ModelInfo> getModelInfo(const String& modelId) const {
        String canonical = getCanonicalName(modelId);
        auto it = configs_.find(canonical);
        if (it != configs_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /// Get display name for model
    String getDisplayName(const String& modelId) const {
        bool has1m = has1MContext(modelId);
        String base = strip1MSuffix(modelId);

        auto config = getModelInfo(base);
        if (config) {
            String name = config->displayName;
            if (has1m) {
                name += " (1M context)";
            }
            return name;
        }

        // Unknown model, return as-is
        return modelId;
    }

    /// Check if model supports 1M context
    bool supports1MContext(const String& modelId) const {
        String canonical = getCanonicalName(strip1MSuffix(modelId));
        auto it = configs_.find(canonical);
        if (it != configs_.end()) {
            return it->second.supports1M;
        }
        return false;
    }

    /// Get canonical model name (strips provider suffixes, normalizes)
    String getCanonicalName(const String& modelId) const {
        String lower = strip1MSuffix(modelId);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Check for known model patterns
        // Order matters: check more specific versions first
        if (lower.find("claude-opus-4-7") != String::npos) return "claude-opus-4-7";
        if (lower.find("claude-opus-4-6") != String::npos) return "claude-opus-4-6";
        if (lower.find("claude-opus-4-5") != String::npos) return "claude-opus-4-5";
        if (lower.find("claude-opus-4-1") != String::npos) return "claude-opus-4-1";
        if (lower.find("claude-opus-4") != String::npos) return "claude-opus-4";

        if (lower.find("claude-sonnet-4-6") != String::npos) return "claude-sonnet-4-6";
        if (lower.find("claude-sonnet-4-5") != String::npos) return "claude-sonnet-4-5";
        if (lower.find("claude-sonnet-4") != String::npos) return "claude-sonnet-4";

        if (lower.find("claude-haiku-4-5") != String::npos) return "claude-haiku-4-5";
        if (lower.find("claude-haiku-4") != String::npos) return "claude-haiku-4";

        // Claude 3.x
        if (lower.find("claude-3-7-sonnet") != String::npos) return "claude-3-7-sonnet";
        if (lower.find("claude-3-5-sonnet") != String::npos) return "claude-3-5-sonnet";
        if (lower.find("claude-3-5-haiku") != String::npos) return "claude-3-5-haiku";
        if (lower.find("claude-3-opus") != String::npos) return "claude-3-opus";
        if (lower.find("claude-3-sonnet") != String::npos) return "claude-3-sonnet";
        if (lower.find("claude-3-haiku") != String::npos) return "claude-3-haiku";

        // Try generic pattern
        auto match = lower.find("claude-");
        if (match != String::npos) {
            return lower.substr(match);
        }

        return lower;
    }

    /// Get all available models
    std::vector<std::pair<String, String>> getAvailableModels() const {
        return {
            {"opus", "Opus 4.6 - Most capable for complex work"},
            {"sonnet", "Sonnet 4.6 - Best for everyday tasks"},
            {"haiku", "Haiku 4.5 - Fast and efficient"},
            {"opusplan", "Opus in plan mode, Sonnet otherwise"},
            {"best", "Best available model (Opus)"},
            {"", ""},
            {"claude-opus-4-6", "Opus 4.6"},
            {"claude-opus-4-5", "Opus 4.5"},
            {"claude-sonnet-4-6", "Sonnet 4.6"},
            {"claude-sonnet-4-5", "Sonnet 4.5"},
            {"claude-haiku-4-5-20251001", "Haiku 4.5"},
        };
    }

private:
    ModelRegistry() {
        initConfigs();
    }

    std::unordered_map<String, ModelInfo> configs_;

    void initConfigs() {
        // Opus models
        configs_["claude-opus-4-6"] = ModelInfo{
            .id = "claude-opus-4-6",
            .displayName = "Opus 4.6",
            .description = "Most capable for complex work",
            .family = ModelFamily::Opus,
            .version = {4, 6, 0},
            .maxContext = 200000,
            .supports1M = true,
            .supportsThinking = true,
            .inputCostPer1M = 15.0,
            .outputCostPer1M = 75.0
        };

        configs_["claude-opus-4-5"] = ModelInfo{
            .id = "claude-opus-4-5",
            .displayName = "Opus 4.5",
            .description = "Highly capable model",
            .family = ModelFamily::Opus,
            .version = {4, 5, 0},
            .maxContext = 200000,
            .supports1M = true,
            .supportsThinking = true,
            .inputCostPer1M = 15.0,
            .outputCostPer1M = 75.0
        };

        configs_["claude-opus-4"] = ModelInfo{
            .id = "claude-opus-4-20250514",
            .displayName = "Opus 4",
            .description = "Opus 4.0",
            .family = ModelFamily::Opus,
            .version = {4, 0, 0},
            .maxContext = 200000,
            .supports1M = false,
            .supportsThinking = true,
            .inputCostPer1M = 15.0,
            .outputCostPer1M = 75.0
        };

        // Sonnet models
        configs_["claude-sonnet-4-6"] = ModelInfo{
            .id = "claude-sonnet-4-6",
            .displayName = "Sonnet 4.6",
            .description = "Best for everyday tasks",
            .family = ModelFamily::Sonnet,
            .version = {4, 6, 0},
            .maxContext = 200000,
            .supports1M = true,
            .supportsThinking = false,
            .inputCostPer1M = 3.0,
            .outputCostPer1M = 15.0
        };

        configs_["claude-sonnet-4-5"] = ModelInfo{
            .id = "claude-sonnet-4-5-20250929",
            .displayName = "Sonnet 4.5",
            .description = "Balanced performance",
            .family = ModelFamily::Sonnet,
            .version = {4, 5, 0},
            .maxContext = 200000,
            .supports1M = true,
            .supportsThinking = false,
            .inputCostPer1M = 3.0,
            .outputCostPer1M = 15.0
        };

        configs_["claude-sonnet-4"] = ModelInfo{
            .id = "claude-sonnet-4-20250514",
            .displayName = "Sonnet 4",
            .description = "Sonnet 4.0",
            .family = ModelFamily::Sonnet,
            .version = {4, 0, 0},
            .maxContext = 200000,
            .supports1M = false,
            .supportsThinking = false,
            .inputCostPer1M = 3.0,
            .outputCostPer1M = 15.0
        };

        // Haiku models
        configs_["claude-haiku-4-5"] = ModelInfo{
            .id = "claude-haiku-4-5-20251001",
            .displayName = "Haiku 4.5",
            .description = "Fast and efficient",
            .family = ModelFamily::Haiku,
            .version = {4, 5, 0},
            .maxContext = 200000,
            .supports1M = false,
            .supportsThinking = false,
            .inputCostPer1M = 0.8,
            .outputCostPer1M = 4.0
        };

        // Claude 3.x models (for backward compatibility)
        configs_["claude-3-5-sonnet"] = ModelInfo{
            .id = "claude-3-5-sonnet-20241022",
            .displayName = "Claude 3.5 Sonnet",
            .description = "Legacy Claude 3.5 Sonnet",
            .family = ModelFamily::Sonnet,
            .version = {3, 5, 0},
            .maxContext = 200000,
            .supports1M = false,
            .supportsThinking = false,
            .inputCostPer1M = 3.0,
            .outputCostPer1M = 15.0
        };

        configs_["claude-3-5-haiku"] = ModelInfo{
            .id = "claude-3-5-haiku-20241022",
            .displayName = "Claude 3.5 Haiku",
            .description = "Legacy Claude 3.5 Haiku",
            .family = ModelFamily::Haiku,
            .version = {3, 5, 0},
            .maxContext = 200000,
            .supports1M = false,
            .supportsThinking = false,
            .inputCostPer1M = 0.8,
            .outputCostPer1M = 4.0
        };
    }
};

} // namespace claude
