#pragma once

#include "../core/Types.hpp"
#include "AnsiStyle.hpp"
#include <ostream>
#include <sstream>

namespace claude {

/// Unicode figures matching TS version
struct Figures {
    static constexpr const char* BLACK_CIRCLE = "●";      // Assistant message indicator
    static constexpr const char* PROMPT = "❯";            // User prompt symbol
    static constexpr const char* CURSOR = "▏";            // Input cursor indicator
    static constexpr const char* TOOL_PREFIX = "⎿";       // Tool/result prefix (same visual as RESPONSE_PREFIX)
    static constexpr const char* RESPONSE_PREFIX = "⎿";   // MessageResponse prefix (alias)
    static constexpr const char* THINKING_EMOJI = "💭";   // Thinking block icon
    static constexpr const char* LIGHTNING = "⚡";        // Tool execution
    static constexpr const char* CHECK = "✓";             // Success
    static constexpr const char* CROSS = "✗";             // Error
    static constexpr const char* EMPTY_SET = "⊘";         // Cancelled/Rejected
    static constexpr const char* ARROW_RIGHT = "→";       // Continuation
};

/// MessageResponse - matches TS MessageResponse component
/// Wraps content with "  ⎿ " prefix for assistant responses
class MessageResponse {
public:
    explicit MessageResponse(std::ostream& out, int height = -1)
        : out_(out), height_(height) {}

    /// Render a message with the response prefix
    void render(const String& content) {
        // "  ⎿ " prefix (dimmed) + content
        out_ << AnsiStyle::Semantic::TOOL_PREFIX << "  " << Figures::TOOL_PREFIX << " " << AnsiStyle::RESET
             << content;
    }

    /// Render a message with custom prefix style
    void render(const String& content, bool dimContent) {
        out_ << AnsiStyle::Semantic::TOOL_PREFIX << "  " << Figures::TOOL_PREFIX << " " << AnsiStyle::RESET;
        if (dimContent) {
            out_ << AnsiStyle::DIM << content << AnsiStyle::RESET;
        } else {
            out_ << content;
        }
    }

    /// Static helper: format a response string
    static String format(const String& content) {
        std::ostringstream oss;
        oss << AnsiStyle::Semantic::TOOL_PREFIX << "  " << Figures::TOOL_PREFIX << " " << AnsiStyle::RESET
            << content;
        return oss.str();
    }

    /// Static helper: format a dimmed response
    static String formatDim(const String& content) {
        std::ostringstream oss;
        oss << AnsiStyle::Semantic::TOOL_PREFIX << "  " << Figures::TOOL_PREFIX << " " << content
            << AnsiStyle::RESET;
        return oss.str();
    }

    /// Static helper: format a tool result with ✓ or ✗ prefix
    static String formatWithCheck(bool success, const String& content) {
        std::ostringstream oss;
        oss << AnsiStyle::Semantic::TOOL_PREFIX << "  " << Figures::TOOL_PREFIX << " " << AnsiStyle::RESET;
        if (success) {
            oss << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET
                << " " << content;
        } else {
            oss << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET
                << " " << content;
        }
        return oss.str();
    }

    /// Static helper: format a tool badge with per-tool colors
    static String formatToolBadge(const String& toolName) {
        std::ostringstream oss;
        oss << AnsiStyle::toolBgColor(toolName) << AnsiStyle::toolFgColor(toolName)
            << " " << toolName << " " << AnsiStyle::RESET;
        return oss.str();
    }

private:
    std::ostream& out_;
    int height_;  // -1 = auto, 1 = single line compact
};

/// AssistantMessageFormatter - formats assistant text messages
class AssistantMessageFormatter {
public:
    /// Format the start of an assistant message (with ● indicator)
    static String formatStart() {
        std::ostringstream oss;
        oss << AnsiStyle::Semantic::ASSISTANT << "  " << Figures::BLACK_CIRCLE << " " << AnsiStyle::RESET;
        return oss.str();
    }

    /// Format a tool use message with per-tool badge
    static String formatToolUse(const String& toolName, const String& input) {
        std::ostringstream oss;
        oss << MessageResponse::formatToolBadge(toolName);
        if (!input.empty()) {
            oss << " " << truncate(input, 60);
        }
        return MessageResponse::format(oss.str());
    }

    /// Format tool progress message
    static String formatToolProgress(const String& status) {
        return MessageResponse::formatDim(status);
    }

    /// Format tool result (single line compact with ✓/✗)
    static String formatToolResultCompact(const String& summary, bool success = true) {
        return MessageResponse::formatWithCheck(success, summary);
    }

    /// Format tool result with timing
    static String formatToolResultTimed(const String& action, int count, double durationSeconds, bool success = true) {
        std::ostringstream oss;
        oss << action;
        if (count != 1) oss << "s";
        oss << " in " << formatDuration(durationSeconds);
        return MessageResponse::formatWithCheck(success, oss.str());
    }

    /// Format duration as "Xs" or "Xms"
    static String formatDuration(double seconds) {
        std::ostringstream oss;
        if (seconds >= 60.0) {
            int m = static_cast<int>(seconds) / 60;
            int s = static_cast<int>(seconds) % 60;
            oss << m << "m " << s << "s";
        } else if (seconds >= 1.0) {
            oss << static_cast<int>(seconds) << "s";
        } else {
            oss << static_cast<int>(seconds * 1000) << "ms";
        }
        return oss.str();
    }

    /// Format elapsed time for spinner/status display
    static String formatElapsed(int totalSeconds) {
        if (totalSeconds < 60) {
            return std::to_string(totalSeconds) + "s";
        }
        int m = totalSeconds / 60;
        int s = totalSeconds % 60;
        return std::to_string(m) + "m " + std::to_string(s) + "s";
    }

    /// Format token count for display
    static String formatTokenCount(long tokens) {
        if (tokens >= 1000) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << (tokens / 1000.0) << "K";
            return oss.str();
        }
        return std::to_string(tokens);
    }

private:
    static String truncate(const String& s, size_t maxLen) {
        if (s.length() <= maxLen) return s;
        return s.substr(0, maxLen - 3) + "...";
    }
};

/// ToolResultRenderer - tool-specific result formatting (matches TS renderToolResultMessage)
class ToolResultRenderer {
public:
    /// Render Bash tool result with ✓/✗
    static String bashResult(int exitCode, double durationSeconds) {
        std::ostringstream oss;
        if (exitCode == 0) {
            oss << "Ran in " << AssistantMessageFormatter::formatDuration(durationSeconds);
            return MessageResponse::formatWithCheck(true, oss.str());
        } else {
            oss << "Exit code " << exitCode << " in "
                << AssistantMessageFormatter::formatDuration(durationSeconds);
            return MessageResponse::formatWithCheck(false, oss.str());
        }
    }

    /// Render WebSearch tool result
    static String webSearchResult(int searchCount, int resultCount, double durationSeconds) {
        std::ostringstream oss;
        oss << searchCount << " result";
        if (resultCount != 1) oss << "s";
        oss << " in " << AssistantMessageFormatter::formatDuration(durationSeconds);
        return MessageResponse::formatWithCheck(true, oss.str());
    }

    /// Render FileRead tool result
    static String fileReadResult(const String& path, size_t bytes, int lineCount) {
        std::ostringstream oss;
        oss << "Read " << path;
        if (lineCount > 0) {
            oss << " (" << lineCount << " lines)";
        }
        return MessageResponse::formatWithCheck(true, oss.str());
    }

    /// Render FileWrite tool result
    static String fileWriteResult(const String& path, size_t bytes) {
        std::ostringstream oss;
        oss << "Wrote " << bytes << " bytes to " << path;
        return MessageResponse::formatWithCheck(true, oss.str());
    }

    /// Render Grep/Glob tool result
    static String searchResult(const String& toolName, int matchCount, double durationSeconds) {
        std::ostringstream oss;
        oss << matchCount << " match";
        if (matchCount != 1) oss << "es";
        oss << " in " << AssistantMessageFormatter::formatDuration(durationSeconds);
        return MessageResponse::formatWithCheck(true, oss.str());
    }

    /// Render generic tool result
    static String genericResult(const String& toolName, const String& summary, bool success = true) {
        return MessageResponse::formatWithCheck(success, summary);
    }
};

} // namespace claude
