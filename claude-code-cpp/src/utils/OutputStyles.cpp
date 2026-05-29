#include "claude/utils/OutputStyles.hpp"
#include "claude/config/SettingsManager.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace claude {

// ========== Constants ==========

const String DEFAULT_OUTPUT_STYLE_NAME = "default";

// ========== Built-in Styles ==========

String getExplanatoryFeaturePrompt() {
    return R"(## Insights
In order to encourage learning, before and after writing code, always provide brief educational explanations about implementation choices using (with backticks):
"`★ Insight ─────────────────────────────────────`
[2-3 key educational points]
`─────────────────────────────────────────────────`"

These insights should be included in the conversation, not in the codebase. You should generally focus on interesting insights that are specific to the codebase or the code you just wrote, rather than general programming concepts.)";
}

std::map<String, OutputStyleConfigExt> getBuiltinOutputStyles() {
    std::map<String, OutputStyleConfigExt> styles;

    // Default style (null = default behavior)
    // Don't add null entry

    // Explanatory style
    OutputStyleConfigExt explanatory;
    explanatory.name = "Explanatory";
    explanatory.description = "Claude explains its implementation choices and codebase patterns";
    explanatory.source = OutputStyleSource::Builtin;
    explanatory.keepCodingInstructions = true;
    explanatory.prompt =
        "You are an interactive CLI tool that helps users with software engineering tasks. "
        "In addition to software engineering tasks, you should provide educational insights "
        "about the codebase along the way.\n\n"
        "You should be clear and educational, providing helpful explanations while remaining "
        "focused on the task. Balance educational content with task completion. When providing "
        "insights, you may exceed typical length constraints, but remain focused and relevant.\n\n"
        "# Explanatory Style Active\n" + getExplanatoryFeaturePrompt();
    styles[explanatory.name] = explanatory;

    // Learning style
    OutputStyleConfigExt learning;
    learning.name = "Learning";
    learning.description = "Claude pauses and asks you to write small pieces of code for hands-on practice";
    learning.source = OutputStyleSource::Builtin;
    learning.keepCodingInstructions = true;
    learning.prompt =
        "You are an interactive CLI tool that helps users with software engineering tasks. "
        "In addition to software engineering tasks, you should help users learn more about the "
        "codebase through hands-on practice and educational insights.\n\n"
        "You should be collaborative and encouraging. Balance task completion with learning by "
        "requesting user input for meaningful design decisions while handling routine "
        "implementation yourself.\n\n"
        "# Learning Style Active\n"
        "## Requesting Human Contributions\n"
        "In order to encourage learning, ask the human to contribute 2-10 line code pieces when "
        "generating 20+ lines involving:\n"
        "- Design decisions (error handling, data structures)\n"
        "- Business logic with multiple valid approaches\n"
        "- Key algorithms or interface definitions\n\n"
        "### Request Format\n"
        "```\n"
        "• **Learn by Doing**\n"
        "**Context:** [what's built and why this decision matters]\n"
        "**Your Task:** [specific function/section in file]\n"
        "**Guidance:** [trade-offs and constraints to consider]\n"
        "```\n\n"
        "### Key Guidelines\n"
        "- Frame contributions as valuable design decisions, not busy work\n"
        "- You must first add a TODO(human) section into the codebase before making the request\n"
        "- Make sure there is one and only one TODO(human) section in the code\n"
        "- Don't take any action after the Learn by Doing request. Wait for human implementation.\n\n"
        "## Insights\n" + getExplanatoryFeaturePrompt();
    styles[learning.name] = learning;

    return styles;
}

// ========== Output Style Manager ==========

OutputStyleManager& OutputStyleManager::instance() {
    static OutputStyleManager inst;
    return inst;
}

void OutputStyleManager::ensureInitialized() {
    if (initialized_) return;

    builtinStyles_ = getBuiltinOutputStyles();
    currentStyleName_ = DEFAULT_OUTPUT_STYLE_NAME;
    initialized_ = true;
}

std::map<String, OutputStyleConfigExt> OutputStyleManager::getAllStyles() {
    ensureInitialized();

    std::map<String, OutputStyleConfigExt> all;
    // Built-in styles (lowest priority)
    for (const auto& [name, style] : builtinStyles_) {
        all[name] = style;
    }
    // Custom styles (higher priority, can override built-in)
    for (const auto& [name, style] : customStyles_) {
        all[name] = style;
    }
    return all;
}

std::optional<OutputStyleConfigExt> OutputStyleManager::getCurrentStyle() {
    ensureInitialized();

    if (!currentStyleName_.has_value() || *currentStyleName_ == DEFAULT_OUTPUT_STYLE_NAME) {
        return std::nullopt;
    }

    return getStyle(*currentStyleName_);
}

bool OutputStyleManager::setStyle(const String& name) {
    ensureInitialized();

    auto allStyles = getAllStyles();
    if (name == DEFAULT_OUTPUT_STYLE_NAME || allStyles.find(name) != allStyles.end()) {
        currentStyleName_ = name;
        return true;
    }
    return false;
}

std::optional<OutputStyleConfigExt> OutputStyleManager::getStyle(const String& name) {
    ensureInitialized();

    // Check custom styles first
    auto it = customStyles_.find(name);
    if (it != customStyles_.end()) {
        return it->second;
    }

    // Then built-in
    it = builtinStyles_.find(name);
    if (it != builtinStyles_.end()) {
        return it->second;
    }

    return std::nullopt;
}

bool OutputStyleManager::hasCustomStyle() {
    ensureInitialized();
    return currentStyleName_.has_value() &&
           *currentStyleName_ != DEFAULT_OUTPUT_STYLE_NAME;
}

void OutputStyleManager::reload() {
    initialized_ = false;
    customStyles_.clear();
    ensureInitialized();

    // Load output style from SettingsManager
    SettingsManager settings;
    settings.load();
    auto effective = settings.getEffectiveSettings();

    if (effective.contains("outputStyle") && effective["outputStyle"].is_string()) {
        String styleName = effective["outputStyle"].template get<String>();
        if (!styleName.empty() && styleName != DEFAULT_OUTPUT_STYLE_NAME) {
            // Check if it's a known built-in or custom style
            auto allStyles = getAllStyles();
            if (allStyles.find(styleName) != allStyles.end()) {
                currentStyleName_ = styleName;
            } else {
                // Register as a custom style with the name as prompt
                OutputStyleConfigExt custom;
                custom.name = styleName;
                custom.description = "Custom style loaded from settings";
                custom.source = OutputStyleSource::UserSettings;
                custom.keepCodingInstructions = true;
                custom.prompt = "Use the " + styleName + " output style.";
                registerStyle(custom);
                currentStyleName_ = styleName;
            }
            spdlog::debug("OutputStyleManager: loaded style '{}' from settings", styleName);
        }
    }

    // Load custom styles defined in settings
    if (effective.contains("customOutputStyles") && effective["customOutputStyles"].is_object()) {
        for (auto& [name, config] : effective["customOutputStyles"].items()) {
            if (config.is_object() && config.contains("prompt")) {
                OutputStyleConfigExt custom;
                custom.name = name;
                custom.description = config.value("description", "");
                custom.prompt = config.value("prompt", "");
                custom.source = OutputStyleSource::UserSettings;
                custom.keepCodingInstructions = config.value("keepCodingInstructions", true);
                registerStyle(custom);
            }
        }
    }
}

void OutputStyleManager::registerStyle(const OutputStyleConfigExt& style) {
    ensureInitialized();
    customStyles_[style.name] = style;
}

void OutputStyleManager::clearCustomStyles() {
    customStyles_.clear();
}

// ========== Utility Functions ==========

String getOutputStyleSectionForPrompt(const std::optional<OutputStyleConfigExt>& config) {
    if (!config.has_value()) {
        return "";
    }

    std::ostringstream oss;
    oss << "# Output Style: " << config->name << "\n";
    oss << config->prompt << "\n";
    return oss.str();
}

String outputStyleSourceToString(OutputStyleSource source) {
    switch (source) {
        case OutputStyleSource::Builtin: return "built-in";
        case OutputStyleSource::Plugin: return "plugin";
        case OutputStyleSource::UserSettings: return "userSettings";
        case OutputStyleSource::ProjectSettings: return "projectSettings";
        case OutputStyleSource::PolicySettings: return "policySettings";
    }
    return "built-in";
}

} // namespace claude
