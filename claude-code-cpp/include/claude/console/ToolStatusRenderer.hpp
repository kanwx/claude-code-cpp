#pragma once

#include "../core/Types.hpp"
#include "../core/ApiTypes.hpp"

#include <ostream>
#include <vector>
#include <map>

namespace claude {

// Forward declaration — avoid full include of ToolRegistry.hpp
class ToolRegistry;

/// Parsed tool input fields for rendering
struct ToolInputInfo {
    String filePath;           // Read/Write/Edit file_path
    String command;            // Bash command
    String pattern;            // Grep/Glob pattern/query
    String description;        // Human-readable summary
    std::map<String, String> extra; // Additional fields
};

/// Parse tool input JSON into structured fields
ToolInputInfo parseToolInput(const String& toolName, const String& inputJson);

/// 工具状态渲染器 —— 匹配原版 TS 设计
/// 使用 ⎿ 前缀 + per-tool colored badge + ✓/✗ 指示
///
/// 工具调用:   ⎿ [Read] path/to/file
/// 工具结果:   ⎿ ✓ Read path/to/file (10 lines)
/// 工具错误:   ⎿ ✗ Error: file not found
class ToolStatusRenderer {
public:
    explicit ToolStatusRenderer(std::ostream& out);

    /// Render tool invocation start with parsed input summary
    void renderStart(const String& toolName, const String& args);

    /// Render tool invocation start with pre-parsed input info
    void renderStart(const String& toolName, const ToolInputInfo& info);

    /// Render in-progress tool invocation with progress state text
    /// Shows animated dot + badge + activity + dim progress text (e.g. "Running…")
    void renderProgress(const String& toolName, const String& args,
                        ToolProgress progress, bool shouldAnimateDot = true);

    /// Render tool invocation end (result)
    void renderEnd(const String& toolName, const String& result,
                   bool isError = false, double durationSeconds = 0,
                   bool isCancelled = false, bool isRejected = false);

    /// Render a complete tool result with per-tool specialized formatting
    void renderToolResult(const String& toolName, const String& result,
                          const String& inputJson, bool isError, double durationSeconds = 0,
                          bool isCancelled = false, bool isRejected = false);

    /// Render a collapsed group of tool invocations
    static String formatCollapsedGroup(int toolCount,
                                       const std::vector<String>& toolNames,
                                       const std::vector<String>& args);

    /// Format per-tool result summary line (for inline display)
    static String formatToolSummary(const String& toolName, const String& result,
                                    const String& inputJson, bool isError,
                                    double durationSeconds = 0);

    /// Four-state result rendering: success/error/cancelled/rejected
    static String renderResult(const String& toolName, const String& result,
                               bool isError, bool isCancelled, bool isRejected);

    /// Per-tool state dot: ●/○ blinking (in-progress), • (completed),
    /// ✗ (error), ⊘ (cancelled/rejected)
    static String toolStateDot(bool isInProgress, bool isError,
                               bool isCancelled, bool isRejected,
                               bool shouldAnimate);

    /// Set an optional ToolRegistry for dispatching to custom Tool::renderToolResult()
    void setToolRegistry(ToolRegistry* registry) { toolRegistry_ = registry; }

private:
    String truncate(const String& s, size_t maxLen);
    void renderBadge(const String& toolName);
    void renderPrefix();

    // Per-tool result formatters
    void renderBashResult(const String& result, bool isError, double durationSeconds);
    void renderReadResult(const String& result, const ToolInputInfo& info, bool isError);
    void renderWriteResult(const String& result, const ToolInputInfo& info, bool isError);
    void renderEditResult(const String& result, const ToolInputInfo& info, bool isError);
    void renderGrepResult(const String& result, const ToolInputInfo& info, bool isError, double durationSeconds);
    void renderGlobResult(const String& result, const ToolInputInfo& info, bool isError);
    void renderWebFetchResult(const String& result, bool isError, double durationSeconds);
    void renderWebSearchResult(const String& result, bool isError, double durationSeconds);
    void renderLSPResult(const String& result, const ToolInputInfo& info, bool isError);
    void renderAgentResult(const String& result, bool isError, double durationSeconds);
    void renderGenericResult(const String& result, bool isError, double durationSeconds);

    /// Count lines in text
    static int countLines(const String& text);

    /// Extract file path from result text (for Read tool "Read path (N lines)" format)
    static String extractFilePath(const String& result);

    std::ostream& out_;
    ToolRegistry* toolRegistry_ = nullptr;
};

/// Determine if animation should be active
/// Disabled during transcript mode or when a permission dialog is open
bool shouldAnimate(bool permissionDialogOpen, bool transcriptMode);

} // namespace claude
