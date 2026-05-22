#include <claude/console/ToolStatusRenderer.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/MessageResponse.hpp>
#include <claude/tool/ToolRegistry.hpp>

namespace claude {

// ========== Tool Input Parser ==========

/// Simple JSON field extractor — finds "key": "value" patterns
/// Avoids full JSON parser dependency while handling the common cases
static String extractJsonString(const String& json, const String& key) {
    // Look for "key": "value" or "key":"value"
    String searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == String::npos) return "";

    // Skip past the key and find the colon
    pos += searchKey.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':')) pos++;
    if (pos >= json.size()) return "";

    // Expect opening quote
    if (json[pos] != '"') return "";
    pos++;

    // Read until closing quote (handle escaped quotes)
    String value;
    while (pos < json.size()) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            if (next == '"' || next == '\\' || next == '/') {
                value += next;
                pos += 2;
            } else if (next == 'n') {
                value += '\n';
                pos += 2;
            } else if (next == 't') {
                value += '\t';
                pos += 2;
            } else {
                value += json[pos];
                pos++;
            }
        } else if (json[pos] == '"') {
            break;
        } else {
            value += json[pos];
            pos++;
        }
    }
    return value;
}

/// Extract a number from JSON field
static int extractJsonInt(const String& json, const String& key) {
    String searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == String::npos) return -1;

    pos += searchKey.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':')) pos++;
    if (pos >= json.size()) return -1;

    String num;
    while (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) {
        num += json[pos];
        pos++;
    }
    return num.empty() ? -1 : std::atoi(num.c_str());
}

ToolInputInfo parseToolInput(const String& toolName, const String& inputJson) {
    ToolInputInfo info;

    if (toolName == "Bash" || toolName == "BashTool") {
        info.command = extractJsonString(inputJson, "command");
        info.description = extractJsonString(inputJson, "description");
    } else if (toolName == "Read" || toolName == "ReadTool" || toolName == "FileReadTool") {
        info.filePath = extractJsonString(inputJson, "file_path");
        int offset = extractJsonInt(inputJson, "offset");
        if (offset > 0) info.extra["offset"] = std::to_string(offset);
    } else if (toolName == "Write" || toolName == "WriteTool" || toolName == "FileWriteTool") {
        info.filePath = extractJsonString(inputJson, "file_path");
    } else if (toolName == "Edit" || toolName == "EditTool" || toolName == "FileEditTool") {
        info.filePath = extractJsonString(inputJson, "file_path");
        String oldText = extractJsonString(inputJson, "old_string");
        String newText = extractJsonString(inputJson, "new_string");
        if (!oldText.empty()) info.extra["old_string"] = oldText;
        if (!newText.empty()) info.extra["new_string"] = newText;
        bool replaceAll = inputJson.find("\"replace_all\":true") != String::npos ||
                          inputJson.find("\"replace_all\": true") != String::npos;
        if (replaceAll) info.extra["replace_all"] = "true";
    } else if (toolName == "Grep" || toolName == "GrepTool") {
        info.pattern = extractJsonString(inputJson, "pattern");
        if (info.pattern.empty()) info.pattern = extractJsonString(inputJson, "query");
        info.filePath = extractJsonString(inputJson, "path");
    } else if (toolName == "Glob" || toolName == "GlobTool") {
        info.pattern = extractJsonString(inputJson, "pattern");
        info.filePath = extractJsonString(inputJson, "path");
    } else if (toolName == "WebFetch" || toolName == "WebFetchTool") {
        info.filePath = extractJsonString(inputJson, "url");
    } else if (toolName == "WebSearch" || toolName == "WebSearchTool") {
        info.pattern = extractJsonString(inputJson, "query");
    } else if (toolName == "Agent" || toolName == "AgentTool") {
        info.description = extractJsonString(inputJson, "description");
        String prompt = extractJsonString(inputJson, "prompt");
        if (!prompt.empty() && info.description.empty()) {
            info.description = prompt.substr(0, 60);
        }
    } else if (toolName == "LSP" || toolName == "LSPTool") {
        info.filePath = extractJsonString(inputJson, "file_path");
        info.pattern = extractJsonString(inputJson, "operation");
    } else if (toolName == "NotebookEdit" || toolName == "NotebookEditTool") {
        info.filePath = extractJsonString(inputJson, "notebook_path");
    } else if (toolName == "AskUserQuestion" || toolName == "AskUserQuestionTool") {
        info.description = extractJsonString(inputJson, "question");
    }

    return info;
}

// ========== ToolStatusRenderer ==========

ToolStatusRenderer::ToolStatusRenderer(std::ostream& out) : out_(out) {}

void ToolStatusRenderer::renderPrefix() {
    out_ << AnsiStyle::Semantic::TOOL_PREFIX << "  " << Figures::TOOL_PREFIX << " " << AnsiStyle::RESET;
}

void ToolStatusRenderer::renderBadge(const String& toolName) {
    out_ << AnsiStyle::toolBgColor(toolName) << AnsiStyle::toolFgColor(toolName)
         << " " << toolName << " " << AnsiStyle::RESET;
}

void ToolStatusRenderer::renderStart(const String& toolName, const String& args) {
    auto info = parseToolInput(toolName, args);
    renderStart(toolName, info);
}

void ToolStatusRenderer::renderStart(const String& toolName, const ToolInputInfo& info) {
    renderPrefix();
    renderBadge(toolName);

    // Tool-specific input summary
    if (toolName == "Bash" || toolName == "BashTool") {
        if (!info.command.empty()) {
            out_ << " " << AnsiStyle::DIM << truncate(info.command, 60) << AnsiStyle::RESET;
        }
    } else if (toolName == "Read" || toolName == "ReadTool" || toolName == "FileReadTool") {
        if (!info.filePath.empty()) {
            out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
            if (info.extra.count("offset")) {
                out_ << AnsiStyle::DIM << " (line " << info.extra.at("offset") << ")" << AnsiStyle::RESET;
            }
        }
    } else if (toolName == "Write" || toolName == "WriteTool" || toolName == "FileWriteTool") {
        if (!info.filePath.empty()) {
            out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
        }
    } else if (toolName == "Edit" || toolName == "EditTool" || toolName == "FileEditTool") {
        if (!info.filePath.empty()) {
            out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
            if (info.extra.count("replace_all") && info.extra.at("replace_all") == "true") {
                out_ << AnsiStyle::DIM << " (replace all)" << AnsiStyle::RESET;
            }
        }
    } else if (toolName == "Grep" || toolName == "GrepTool") {
        if (!info.pattern.empty()) {
            out_ << " " << AnsiStyle::DIM << "\"" << truncate(info.pattern, 30) << "\"";
            if (!info.filePath.empty()) out_ << " in " << info.filePath;
            out_ << AnsiStyle::RESET;
        }
    } else if (toolName == "Glob" || toolName == "GlobTool") {
        if (!info.pattern.empty()) {
            out_ << " " << AnsiStyle::DIM << truncate(info.pattern, 30);
            if (!info.filePath.empty()) out_ << " in " << info.filePath;
            out_ << AnsiStyle::RESET;
        }
    } else if (toolName == "WebFetch" || toolName == "WebFetchTool") {
        if (!info.filePath.empty()) {
            out_ << " " << AnsiStyle::DIM << truncate(info.filePath, 50) << AnsiStyle::RESET;
        }
    } else if (toolName == "WebSearch" || toolName == "WebSearchTool") {
        if (!info.pattern.empty()) {
            out_ << " " << AnsiStyle::DIM << "\"" << truncate(info.pattern, 40) << "\"" << AnsiStyle::RESET;
        }
    } else if (toolName == "Agent" || toolName == "AgentTool") {
        if (!info.description.empty()) {
            out_ << " " << AnsiStyle::DIM << truncate(info.description, 60) << AnsiStyle::RESET;
        }
    } else if (toolName == "LSP" || toolName == "LSPTool") {
        if (!info.filePath.empty()) {
            out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
        }
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderEnd(const String& toolName, const String& result,
                                    bool isError, double durationSeconds,
                                    bool isCancelled, bool isRejected) {
    renderPrefix();

    if (isCancelled) {
        out_ << AnsiStyle::Semantic::TOOL_CANCELLED << Figures::EMPTY_SET << " Canceled"
             << AnsiStyle::RESET << "\n";
        return;
    }
    if (isRejected) {
        out_ << AnsiStyle::Semantic::TOOL_REJECTED << Figures::EMPTY_SET << " Rejected"
             << AnsiStyle::RESET << "\n";
        return;
    }
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET << " ";
        String shortResult = truncate(result, 200);
        out_ << AnsiStyle::DIM << shortResult << AnsiStyle::RESET;
    } else {
        out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET << " ";
        String shortResult = truncate(result, 200);
        out_ << AnsiStyle::DIM << shortResult << AnsiStyle::RESET;
        if (durationSeconds > 0) {
            out_ << AnsiStyle::DIM << " in " << AssistantMessageFormatter::formatDuration(durationSeconds)
                 << AnsiStyle::RESET;
        }
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderToolResult(const String& toolName, const String& result,
                                           const String& inputJson, bool isError, double durationSeconds,
                                           bool isCancelled, bool isRejected) {
    // Handle cancelled/rejected before per-tool rendering — these states
    // always render the same way regardless of tool type.
    if (isCancelled || isRejected) {
        renderPrefix();
        if (isCancelled) {
            out_ << AnsiStyle::Semantic::TOOL_CANCELLED << Figures::EMPTY_SET << " Canceled"
                 << AnsiStyle::RESET << "\n";
        } else {
            out_ << AnsiStyle::Semantic::TOOL_REJECTED << Figures::EMPTY_SET << " Rejected"
                 << AnsiStyle::RESET << "\n";
        }
        return;
    }

    // Try tool's custom rendering first
    if (toolRegistry_) {
        auto* tool = toolRegistry_->findByName(toolName);
        if (tool) {
            String customResult = tool->renderToolResult(result, isError, isCancelled, isRejected);
            if (!customResult.empty()) {
                renderPrefix();
                out_ << customResult << "\n";
                return;
            }
        }
    }

    auto info = parseToolInput(toolName, inputJson);

    renderPrefix();

    if (toolName == "Bash" || toolName == "BashTool") {
        renderBashResult(result, isError, durationSeconds);
    } else if (toolName == "Read" || toolName == "ReadTool" || toolName == "FileReadTool") {
        renderReadResult(result, info, isError);
    } else if (toolName == "Write" || toolName == "WriteTool" || toolName == "FileWriteTool") {
        renderWriteResult(result, info, isError);
    } else if (toolName == "Edit" || toolName == "EditTool" || toolName == "FileEditTool") {
        renderEditResult(result, info, isError);
    } else if (toolName == "Grep" || toolName == "GrepTool") {
        renderGrepResult(result, info, isError, durationSeconds);
    } else if (toolName == "Glob" || toolName == "GlobTool") {
        renderGlobResult(result, info, isError);
    } else if (toolName == "WebFetch" || toolName == "WebFetchTool") {
        renderWebFetchResult(result, isError, durationSeconds);
    } else if (toolName == "WebSearch" || toolName == "WebSearchTool") {
        renderWebSearchResult(result, isError, durationSeconds);
    } else if (toolName == "LSP" || toolName == "LSPTool") {
        renderLSPResult(result, info, isError);
    } else if (toolName == "Agent" || toolName == "AgentTool") {
        renderAgentResult(result, isError, durationSeconds);
    } else {
        renderGenericResult(result, isError, durationSeconds);
    }
}

// ========== Per-tool result formatters ==========

void ToolStatusRenderer::renderBashResult(const String& result, bool isError, double durationSeconds) {
    // Extract exit code
    int exitCode = 0;
    if (isError) {
        size_t pos = result.find("Exit code ");
        if (pos != String::npos) {
            exitCode = std::atoi(result.c_str() + pos + 10);
        } else {
            exitCode = 1;
        }
    }

    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << AnsiStyle::DIM << "Bash" << AnsiStyle::RESET;
        out_ << " " << AnsiStyle::Semantic::TOOL_ERROR << "exit " << exitCode << AnsiStyle::RESET;
        if (durationSeconds > 0) {
            out_ << AnsiStyle::DIM << " in " << AssistantMessageFormatter::formatDuration(durationSeconds)
                 << AnsiStyle::RESET;
        }

        // Show stderr/error output (first few lines)
        String stderrPart;
        size_t stderrPos = result.find("STDERR:");
        if (stderrPos != String::npos) {
            stderrPart = result.substr(stderrPos + 7);
        } else {
            stderrPart = result;
        }
        // Remove exit code line if present
        size_t exitPos = stderrPart.find("Exit code ");
        if (exitPos != String::npos) {
            size_t endLine = stderrPart.find('\n', exitPos);
            if (endLine != String::npos) {
                stderrPart = stderrPart.substr(0, exitPos) + stderrPart.substr(endLine + 1);
            } else {
                stderrPart = stderrPart.substr(0, exitPos);
            }
        }
        if (!stderrPart.empty()) {
            // Show up to 5 lines of error output
            std::istringstream stream(stderrPart);
            String line;
            int lineCount = 0;
            while (std::getline(stream, line) && lineCount < 5) {
                if (!line.empty()) {
                    out_ << "\n" << AnsiStyle::DIM << "    " << Figures::TOOL_PREFIX << " "
                         << AnsiStyle::Semantic::TOOL_ERROR << truncate(line, 120)
                         << AnsiStyle::RESET;
                    lineCount++;
                }
            }
            int totalLines = countLines(stderrPart);
            if (totalLines > 5) {
                out_ << "\n" << AnsiStyle::DIM << "    ... (" << (totalLines - 5) << " more lines)"
                     << AnsiStyle::RESET;
            }
        }
    } else {
        out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
        out_ << " " << AnsiStyle::DIM << "Bash" << AnsiStyle::RESET;
        if (durationSeconds > 0) {
            out_ << AnsiStyle::DIM << " ran in " << AssistantMessageFormatter::formatDuration(durationSeconds)
                 << AnsiStyle::RESET;
        }
        // Show first few lines of output
        if (!result.empty()) {
            std::istringstream stream(result);
            String line;
            int lineCount = 0;
            while (std::getline(stream, line) && lineCount < 3) {
                if (!line.empty()) {
                    out_ << "\n" << AnsiStyle::DIM << "    " << Figures::TOOL_PREFIX << " "
                         << truncate(line, 120) << AnsiStyle::RESET;
                    lineCount++;
                }
            }
            int totalLines = countLines(result);
            if (totalLines > 3) {
                out_ << "\n" << AnsiStyle::DIM << "    ... (" << (totalLines - 3) << " more lines)"
                     << AnsiStyle::RESET;
            }
        }
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderReadResult(const String& result, const ToolInputInfo& info, bool isError) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    int lineCount = countLines(result);
    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "Read" << AnsiStyle::RESET;

    if (!info.filePath.empty()) {
        out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
    }
    out_ << AnsiStyle::DIM << " (" << lineCount << " line" << (lineCount != 1 ? "s" : "") << ")"
         << AnsiStyle::RESET << "\n";
}

void ToolStatusRenderer::renderWriteResult(const String& result, const ToolInputInfo& info, bool isError) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "Write" << AnsiStyle::RESET;

    if (!info.filePath.empty()) {
        out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderEditResult(const String& result, const ToolInputInfo& info, bool isError) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "Edit" << AnsiStyle::RESET;

    if (!info.filePath.empty()) {
        out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
    }

    // Show diff summary if available
    if (info.extra.count("old_string") && info.extra.count("new_string")) {
        auto& oldStr = info.extra.at("old_string");
        auto& newStr = info.extra.at("new_string");
        int oldLines = countLines(oldStr);
        int newLines = countLines(newStr);
        out_ << AnsiStyle::DIM << " (" << oldLines << " line" << (oldLines != 1 ? "s" : "")
             << " → " << newLines << " line" << (newLines != 1 ? "s" : "") << ")"
             << AnsiStyle::RESET;
    }

    if (info.extra.count("replace_all") && info.extra.at("replace_all") == "true") {
        out_ << AnsiStyle::DIM << " [replace all]" << AnsiStyle::RESET;
    }
    out_ << "\n";

    // Render inline diff if result contains diff content
    if (!result.empty() && (result.find("-") != String::npos || result.find("+") != String::npos)) {
        // Show first few diff lines inline
        std::istringstream stream(result);
        String line;
        int lineCount = 0;
        while (std::getline(stream, line) && lineCount < 10) {
            out_ << AnsiStyle::DIM << "    " << Figures::TOOL_PREFIX << " " << AnsiStyle::RESET;
            if (line.starts_with("+") && !line.starts_with("++")) {
                out_ << AnsiStyle::Semantic::DIFF_ADD << line << AnsiStyle::RESET;
            } else if (line.starts_with("-") && !line.starts_with("--")) {
                out_ << AnsiStyle::Semantic::DIFF_REMOVE << line << AnsiStyle::RESET;
            } else {
                out_ << AnsiStyle::DIM << line << AnsiStyle::RESET;
            }
            out_ << "\n";
            lineCount++;
        }
        int totalLines = countLines(result);
        if (totalLines > 10) {
            out_ << AnsiStyle::DIM << "    ... (" << (totalLines - 10) << " more diff lines)"
                 << AnsiStyle::RESET << "\n";
        }
    }
}

void ToolStatusRenderer::renderGrepResult(const String& result, const ToolInputInfo& info,
                                           bool isError, double durationSeconds) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    // Count matches
    int matchCount = countLines(result);
    if (result.empty()) matchCount = 0;

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "Grep" << AnsiStyle::RESET;

    if (!info.pattern.empty()) {
        out_ << " " << AnsiStyle::DIM << "\"" << truncate(info.pattern, 20) << "\"" << AnsiStyle::RESET;
    }

    if (matchCount == 0) {
        out_ << AnsiStyle::DIM << " — no matches" << AnsiStyle::RESET;
    } else {
        out_ << AnsiStyle::DIM << " (" << matchCount << " match" << (matchCount != 1 ? "es" : "") << ")"
             << AnsiStyle::RESET;
    }
    if (durationSeconds > 0) {
        out_ << AnsiStyle::DIM << " in " << AssistantMessageFormatter::formatDuration(durationSeconds)
             << AnsiStyle::RESET;
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderGlobResult(const String& result, const ToolInputInfo& info, bool isError) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    int fileCount = countLines(result);
    if (result.empty()) fileCount = 0;

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "Glob" << AnsiStyle::RESET;

    if (!info.pattern.empty()) {
        out_ << " " << AnsiStyle::DIM << truncate(info.pattern, 20) << AnsiStyle::RESET;
    }

    if (fileCount == 0) {
        out_ << AnsiStyle::DIM << " — no files" << AnsiStyle::RESET;
    } else {
        out_ << AnsiStyle::DIM << " (" << fileCount << " file" << (fileCount != 1 ? "s" : "") << ")"
             << AnsiStyle::RESET;
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderWebFetchResult(const String& result, bool isError, double durationSeconds) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "WebFetch" << AnsiStyle::RESET;
    if (durationSeconds > 0) {
        out_ << AnsiStyle::DIM << " in " << AssistantMessageFormatter::formatDuration(durationSeconds)
             << AnsiStyle::RESET;
    }
    int lineCount = countLines(result);
    out_ << AnsiStyle::DIM << " (" << lineCount << " line" << (lineCount != 1 ? "s" : "") << ")"
         << AnsiStyle::RESET << "\n";
}

void ToolStatusRenderer::renderWebSearchResult(const String& result, bool isError, double durationSeconds) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    // Count result items (search results typically have numbered items)
    int resultCount = countLines(result);

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "WebSearch" << AnsiStyle::RESET;
    if (resultCount > 0) {
        out_ << AnsiStyle::DIM << " (" << resultCount << " result" << (resultCount != 1 ? "s" : "") << ")"
             << AnsiStyle::RESET;
    }
    if (durationSeconds > 0) {
        out_ << AnsiStyle::DIM << " in " << AssistantMessageFormatter::formatDuration(durationSeconds)
             << AnsiStyle::RESET;
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderLSPResult(const String& result, const ToolInputInfo& info, bool isError) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "LSP" << AnsiStyle::RESET;
    if (!info.filePath.empty()) {
        out_ << " " << AnsiStyle::DIM << info.filePath << AnsiStyle::RESET;
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderAgentResult(const String& result, bool isError, double durationSeconds) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << AnsiStyle::DIM << "Agent" << AnsiStyle::RESET;
        out_ << " " << truncate(result, 120) << AnsiStyle::RESET << "\n";
        return;
    }

    out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::DIM << "Agent" << AnsiStyle::RESET;
    if (durationSeconds > 0) {
        out_ << AnsiStyle::DIM << " in " << AssistantMessageFormatter::formatDuration(durationSeconds)
             << AnsiStyle::RESET;
    }
    // Show brief result summary
    if (!result.empty()) {
        String summary = truncate(result, 100);
        out_ << AnsiStyle::DIM << " — " << summary << AnsiStyle::RESET;
    }
    out_ << "\n";
}

void ToolStatusRenderer::renderGenericResult(const String& result, bool isError, double durationSeconds) {
    if (isError) {
        out_ << AnsiStyle::Semantic::TOOL_ERROR << Figures::CROSS << AnsiStyle::RESET;
        out_ << " " << truncate(result, 200) << AnsiStyle::RESET << "\n";
    } else {
        out_ << AnsiStyle::Semantic::TOOL_SUCCESS << Figures::CHECK << AnsiStyle::RESET;
        out_ << " " << AnsiStyle::DIM << truncate(result, 200) << AnsiStyle::RESET;
        if (durationSeconds > 0) {
            out_ << AnsiStyle::DIM << " in " << AssistantMessageFormatter::formatDuration(durationSeconds)
                 << AnsiStyle::RESET;
        }
        out_ << "\n";
    }
}

// ========== Static helpers ==========

String ToolStatusRenderer::renderResult(const String& toolName, const String& result,
                                         bool isError, bool isCancelled, bool isRejected) {
    if (isCancelled) {
        return String(AnsiStyle::DIM) + Figures::EMPTY_SET + " Canceled" + AnsiStyle::RESET;
    }
    if (isRejected) {
        return String(AnsiStyle::Semantic::TOOL_REJECTED) + Figures::EMPTY_SET + " Rejected" + AnsiStyle::RESET;
    }
    if (isError) {
        String truncated = result.size() > 200 ? result.substr(0, 200) + "..." : result;
        return String(AnsiStyle::RED) + Figures::CROSS + " " + truncated + AnsiStyle::RESET;
    }
    // Success — green check + result
    return String(AnsiStyle::GREEN) + Figures::CHECK + " " + result + AnsiStyle::RESET;
}

String ToolStatusRenderer::formatToolSummary(const String& toolName, const String& result,
                                              const String& inputJson, bool isError,
                                              double durationSeconds) {
    std::ostringstream oss;
    ToolStatusRenderer renderer(oss);
    renderer.renderToolResult(toolName, result, inputJson, isError, durationSeconds,
                              /*isCancelled=*/false, /*isRejected=*/false);
    return oss.str();
}

String ToolStatusRenderer::formatCollapsedGroup(int toolCount,
                                                  const std::vector<String>& toolNames,
                                                  const std::vector<String>& args) {
    std::ostringstream oss;
    oss << AnsiStyle::Semantic::TOOL_PREFIX << "  " << Figures::TOOL_PREFIX << " " << AnsiStyle::RESET;
    oss << "[" << toolCount << " tool use";
    if (toolCount != 1) oss << "s";
    oss << "] ";

    bool first = true;
    for (size_t i = 0; i < toolNames.size() && i < 5; ++i) {
        if (!first) oss << ", ";
        first = false;
        oss << AnsiStyle::toolFgColor(toolNames[i]) << toolNames[i] << AnsiStyle::RESET;
        if (i < args.size() && !args[i].empty()) {
            String shortArg = args[i].length() > 20 ? args[i].substr(0, 17) + "..." : args[i];
            oss << " " << shortArg;
        }
    }
    if (static_cast<int>(toolNames.size()) > 5) {
        oss << ", ...";
    }
    oss << " " << AnsiStyle::DIM << "(ctrl+o to expand)" << AnsiStyle::RESET;
    return oss.str();
}

int ToolStatusRenderer::countLines(const String& text) {
    if (text.empty()) return 0;
    int count = 1;
    for (char c : text) {
        if (c == '\n') count++;
    }
    return count;
}

String ToolStatusRenderer::extractFilePath(const String& result) {
    size_t pos = result.find('(');
    if (pos != String::npos && pos > 0) {
        return result.substr(0, pos - 1);
    }
    return result;
}

String ToolStatusRenderer::truncate(const String& s, size_t maxLen) {
    if (s.length() <= maxLen) return s;
    return s.substr(0, maxLen - 3) + "...";
}

} // namespace claude
