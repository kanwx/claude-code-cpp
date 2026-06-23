#include "claude/ui/ToolResultFormatter.hpp"
#include "claude/ui/PathDisplay.hpp"
#include "claude/stream/ContentBlock.hpp"
#include <sstream>
#include <string>

namespace claude {

namespace {

/// Strip a leading prefix from string if present, and return the cleaned path.
/// Handles " from /path" → "/path" and " to /path" → "/path"
String cleanFilePath(const String& raw) {
    String s = raw;
    // Strip leading " from "
    if (s.size() > 6 && s.substr(0, 6) == " from ") {
        s = s.substr(6);
    }
    // Strip leading " to "
    else if (s.size() > 4 && s.substr(0, 4) == " to ") {
        s = s.substr(4);
    }
    // Strip leading "to " (without leading space)
    else if (s.size() > 3 && s.substr(0, 3) == "to ") {
        s = s.substr(3);
    }
    return s;
}

} // anonymous namespace

ToolDisplayModel formatToolResult(const ContentBlock& block) {
    ToolDisplayModel m;
    m.toolName = block.toolName;
    m.isError = block.summary.isError;
    m.errorText = block.summary.errorText;
    m.primaryText = block.summary.primaryText;
    m.secondaryText = block.summary.secondaryText;
    m.expandHint = block.summary.expandHint;
    m.isCancelled = (block.resultStatus == ToolResultStatus::Cancelled);
    m.isRejected = (block.resultStatus == ToolResultStatus::Rejected);

    // Per-tool extraction from summary text
    if (m.toolName == "Read") {
        auto& pt = block.summary.primaryText;
        auto linesPos = pt.find(" lines");
        if (linesPos != String::npos) {
            try { m.lineCount = std::stoi(pt.substr(0, linesPos)); }
            catch (...) { m.lineCount = 0; }
        }
        if (!block.summary.secondaryText.empty()) {
            m.filePath = cleanFilePath(block.summary.secondaryText);
        } else {
            auto fromPos = pt.find(" from ");
            if (fromPos != String::npos) {
                m.filePath = pt.substr(fromPos + 6);
            }
        }
    }
    else if (m.toolName == "Grep") {
        auto& pt = block.summary.primaryText;
        auto foundPos = pt.find("Found ");
        auto matchPos = pt.find(" matches");
        if (foundPos != String::npos && matchPos != String::npos) {
            try { m.matchCount = std::stoi(pt.substr(foundPos + 6, matchPos - foundPos - 6)); }
            catch (...) { m.matchCount = 0; }
        }
    }
    else if (m.toolName == "Glob" || m.toolName == "LS") {
        auto& pt = block.summary.primaryText;
        auto foundPos = pt.find("Found ");
        auto filePos = pt.find(" files");
        if (foundPos != String::npos && filePos != String::npos) {
            try { m.fileCount = std::stoi(pt.substr(foundPos + 6, filePos - foundPos - 6)); }
            catch (...) { m.fileCount = 0; }
        }
    }
    else if (m.toolName == "Bash") {
        m.command = block.summary.primaryText;
    }
    else if (m.toolName == "Edit" || m.toolName == "Write") {
        auto& pt = block.summary.primaryText;
        auto addedPos = pt.find("Added ");
        auto removedPos = pt.find("Removed ");
        if (addedPos != String::npos) {
            auto linesPos = pt.find(" lines", addedPos);
            if (linesPos != String::npos) {
                try { m.linesAdded = std::stoi(pt.substr(addedPos + 6, linesPos - addedPos - 6)); }
                catch (...) { m.linesAdded = 0; }
            }
        }
        if (removedPos != String::npos) {
            auto linesPos = pt.find(" lines", removedPos);
            if (linesPos != String::npos) {
                try { m.linesRemoved = std::stoi(pt.substr(removedPos + 8, linesPos - removedPos - 8)); }
                catch (...) { m.linesRemoved = 0; }
            }
        }
        if (!block.summary.secondaryText.empty()) {
            m.filePath = cleanFilePath(block.summary.secondaryText);
        }
    }
    else if (m.toolName == "WebSearch") {
        auto& pt = block.summary.primaryText;
        auto foundPos = pt.find("Found ");
        auto resultsPos = pt.find(" results");
        if (foundPos != String::npos && resultsPos != String::npos) {
            try { m.resultCount = std::stoi(pt.substr(foundPos + 6, resultsPos - foundPos - 6)); }
            catch (...) { m.resultCount = 0; }
        }
    }
    else if (m.toolName == "WebFetch") {
        m.pageTitle = block.summary.primaryText;
    }

    return m;
}

String ToolDisplayModel::toDisplayText() const {
    if (toolName == "Read" && lineCount > 0) {
        return filePath.empty()
            ? "Read " + std::to_string(lineCount) + " lines"
            : truncatePathForDisplay(filePath) + " (" + std::to_string(lineCount) + " lines)";
    }
    if (toolName == "Grep" && matchCount > 0) {
        return "Found " + std::to_string(matchCount) + " matches";
    }
    if ((toolName == "Glob" || toolName == "LS") && fileCount > 0) {
        return "Found " + std::to_string(fileCount) + " files";
    }
    if (toolName == "Edit" || toolName == "Write") {
        std::ostringstream oss;
        if (linesAdded > 0) oss << "Added " << linesAdded << " lines";
        if (linesRemoved > 0) {
            if (oss.tellp() > 0) oss << ", ";
            oss << "Removed " << linesRemoved << " lines";
        }
        if (oss.tellp() == 0) return primaryText.empty() ? "completed" : primaryText;
        return oss.str();
    }
    if (toolName == "Bash") {
        return primaryText.empty() ? "Done" : primaryText;
    }
    if (toolName == "WebSearch" && resultCount > 0) {
        return "Found " + std::to_string(resultCount) + " results";
    }
    if (toolName == "WebFetch") {
        return pageTitle.empty() ? "Fetched" : pageTitle;
    }
    return primaryText.empty() ? "completed" : primaryText;
}

} // namespace claude
