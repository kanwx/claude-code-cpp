#include <claude/tool/impl/FileWriteTool.hpp>
#include <claude/utils/FileCache.hpp>
#include <claude/utils/FileHistory.hpp>
#include <claude/lsp/LspManager.hpp>
#include <fstream>
#include <cstdlib>
#include <regex>

namespace claude {

namespace {

/// Check if a path falls under a protected system directory
bool isSystemPath(const String& path) {
    static const std::vector<String> systemPrefixes = {
        "/etc/",
        "/usr/",
        "/bin/",
        "/sbin/",
        "/System/",
        "/Library/System/",
        "/private/etc/"
    };

    for (const auto& prefix : systemPrefixes) {
        if (path.size() >= prefix.size() &&
            path.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

/// Check if a path is a hidden file (dotfile) in HOME, excluding .claude/
bool isHiddenFileInHome(const String& path) {
    const char* home = std::getenv("HOME");
    if (!home) return false;

    String homeStr = home;
    if (homeStr.back() != '/') homeStr += '/';

    // Must be inside HOME
    if (path.size() <= homeStr.size() ||
        path.compare(0, homeStr.size(), homeStr) != 0) {
        return false;
    }

    // Get the relative part after HOME/
    String relative = path.substr(homeStr.size());

    // First path component starts with a dot (hidden file/dir)
    auto slashPos = relative.find('/');
    String firstComponent = (slashPos != String::npos) ? relative.substr(0, slashPos) : relative;

    if (firstComponent.empty() || firstComponent[0] != '.') {
        return false;
    }

    // Allow .claude/ directory
    if (firstComponent == ".claude") {
        return false;
    }

    return true;
}

int parseWriteLineCount(const String& result) {
    std::regex lineRegex(R"((\d+) lines)");
    std::smatch match;
    if (std::regex_search(result, match, lineRegex)) {
        return std::stoi(match[1].str());
    }
    return 0;
}

String parseWritePath(const String& result) {
    // Extract path from "Successfully wrote to /path/to/file (...)"
    auto pos = result.find("Successfully wrote to ");
    if (pos != String::npos) {
        String after = result.substr(pos + 22); // len of "Successfully wrote to "
        auto endPos = after.find(" (");
        if (endPos != String::npos) return after.substr(0, endPos);
        // Fallback: trim trailing whitespace/newline
        while (!after.empty() && (after.back() == '\n' || after.back() == ' ')) after.pop_back();
        return after;
    }
    return "";
}

} // anonymous namespace

PermissionResult FileWriteTool::checkPermission(const Json& input, ToolContext& context) {
    String filePath = input.value("file_path", "");
    if (filePath.empty()) {
        return PermissionResult::deny("No file_path specified");
    }

    if (isSystemPath(filePath)) {
        return PermissionResult::deny("Writing to system directories is blocked: " + filePath);
    }

    if (isHiddenFileInHome(filePath)) {
        return PermissionResult::ask("Writing to hidden file in HOME directory: " + filePath);
    }

    return PermissionResult::allow();
}

String FileWriteTool::execute(const Json& input, ToolContext& context) {
    String filePath = input["file_path"];
    String content = input["content"];

    std::filesystem::path path(filePath);
    if (!path.is_absolute()) {
        path = context.workDir / path;
    }

    // Read-before-write enforcement: check if file was modified since last read
    if (std::filesystem::exists(path)) {
        if (FileCache::instance().isStale(path.string())) {
            return "Error: File has been modified since last read. Re-read the file before writing to it. "
                   "Use the Read tool to read the current file contents first.";
        }
    } else {
        // Writing a new file — check that the parent directory exists
        auto parentDir = path.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
            return "Error: Parent directory does not exist: " + parentDir.string();
        }
    }

    // 创建父目录
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // Backup file before writing (if it already exists)
    if (std::filesystem::exists(path)) {
        claude::utils::backupFile(path);
    }

    std::ofstream file(path);
    if (!file) {
        return "Error: Cannot write to file: " + path.string();
    }

    file << content;

    // Update file cache after successful write
    FileCache::instance().invalidate(path.string());

    // Notify LSP servers of the file change
    auto& lspManager = lsp::LspManager::instance();
    lspManager.notifyDidChange(path, content);

    // Count lines in content
    int lineCount = 0;
    for (char c : content) { if (c == '\n') lineCount++; }
    if (!content.empty() && content.back() != '\n') lineCount++;

    return "Successfully wrote to " + path.string() +
           " (" + std::to_string(content.size()) + " bytes, " + std::to_string(lineCount) + " lines)";
}

ToolResultSummary FileWriteTool::renderToolResult(const String& result, bool isError,
                                       bool isCancelled, bool isRejected) const {
    if (isError) return ToolResultSummary::error("Error writing file");
    if (isCancelled) return ToolResultSummary::dim("Interrupted" "\xe2\x88\x99" " What should Claude do instead?");
    if (isRejected) return ToolResultSummary::dim("Tool use rejected");
    int lines = parseWriteLineCount(result);
    String writePath = parseWritePath(result);
    return ToolResultSummary::success("Wrote " + std::to_string(lines) + " lines", /*bold=*/true, "to " + writePath);
}

} // namespace claude
