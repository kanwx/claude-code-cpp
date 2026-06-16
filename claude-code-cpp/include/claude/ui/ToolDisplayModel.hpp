#pragma once

#include <string>
#include "claude/core/ApiTypes.hpp"

namespace claude {

/// Structured per-tool display data extracted from ToolResultSummary
/// and the raw tool execution context.
struct ToolDisplayModel {
    String toolName;

    // ---- Read tool ----
    String filePath;      // e.g., "src/main.cpp"
    int lineCount = 0;    // e.g., 328

    // ---- Grep tool ----
    int matchCount = 0;
    String pattern;       // search pattern

    // ---- Glob/LS tool ----
    int fileCount = 0;

    // ---- Bash tool ----
    int exitCode = 0;
    String duration;      // e.g., "1.2s"
    String command;       // first line of the command

    // ---- Edit/Write tool ----
    int linesAdded = 0;
    int linesRemoved = 0;

    // ---- WebSearch ----
    int resultCount = 0;
    String searchQuery;

    // ---- WebFetch ----
    String pageTitle;
    int contentSize = 0;  // bytes

    // ---- Generic ----
    String primaryText;   // fallback display text
    String secondaryText; // detail below primary
    String expandHint;    // "[Ctrl+O to expand]" or ""
    bool isError = false;
    String errorText;
    bool isCancelled = false;
    bool isRejected = false;

    /// Build the display text string from structured per-tool data.
    String toDisplayText() const;
};

} // namespace claude
