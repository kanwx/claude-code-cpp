#include <claude/tool/impl/FileEditTool.hpp>
#include <claude/utils/FileCache.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

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

} // anonymous namespace

PermissionResult FileEditTool::checkPermission(const Json& input, ToolContext& context) {
    String filePath = input.value("file_path", "");
    if (filePath.empty()) {
        return PermissionResult::deny("No file_path specified");
    }

    if (isSystemPath(filePath)) {
        return PermissionResult::deny("Editing system files is blocked: " + filePath);
    }

    return PermissionResult::allow();
}

extern FileCache& getGlobalFileCache();

/// Generate a unified diff between original and modified content
static String generateDiff(const String& original, const String& modified,
                           const String& filePath, const String& oldString,
                           const String& newString) {
    // Line-based diff: find the changed region
    std::istringstream origStream(original);
    std::istringstream modStream(modified);

    std::vector<String> origLines;
    std::vector<String> modLines;
    String line;

    while (std::getline(origStream, line)) origLines.push_back(line);
    while (std::getline(modStream, line)) modLines.push_back(line);

    // Find first and last differing lines
    size_t firstDiff = 0;
    size_t minLen = std::min(origLines.size(), modLines.size());

    while (firstDiff < minLen && origLines[firstDiff] == modLines[firstDiff]) {
        firstDiff++;
    }

    size_t lastOrig = origLines.size();
    size_t lastMod = modLines.size();

    while (lastOrig > firstDiff && lastMod > firstDiff &&
           origLines[lastOrig - 1] == modLines[lastMod - 1]) {
        lastOrig--;
        lastMod--;
    }

    // Build unified diff
    // Context lines (3 before, 3 after)
    size_t ctxStart = (firstDiff >= 3) ? firstDiff - 3 : 0;
    size_t ctxEndOrig = std::min(lastOrig + 3, origLines.size());
    size_t ctxEndMod = std::min(lastMod + 3, modLines.size());

    std::ostringstream diff;
    diff << "--- a/" << filePath << "\n";
    diff << "+++ b/" << filePath << "\n";
    diff << "@@ -" << (ctxStart + 1) << "," << (ctxEndOrig - ctxStart)
         << " +" << (ctxStart + 1) << "," << (ctxEndMod - ctxStart) << " @@\n";

    // Context before
    for (size_t i = ctxStart; i < firstDiff; ++i) {
        diff << " " << origLines[i] << "\n";
    }

    // Removed lines
    for (size_t i = firstDiff; i < lastOrig; ++i) {
        diff << "-" << origLines[i] << "\n";
    }

    // Added lines
    for (size_t i = firstDiff; i < lastMod; ++i) {
        diff << "+" << modLines[i] << "\n";
    }

    // Context after
    size_t afterStart = lastOrig;
    for (size_t i = afterStart; i < ctxEndOrig; ++i) {
        diff << " " << origLines[i] << "\n";
    }

    return diff.str();
}

String FileEditTool::execute(const Json& input, ToolContext& context) {
    String filePath = input["file_path"];
    String oldString = input["old_string"];
    String newString = input["new_string"];
    bool replaceAll = input.value("replace_all", false);

    std::filesystem::path path(filePath);
    if (!path.is_absolute()) {
        path = context.workDir / path;
    }

    if (!std::filesystem::exists(path)) {
        return "Error: File does not exist: " + path.string();
    }

    // ========== Staleness check ==========
    if (FileCache::instance().isStale(path.string())) {
        return "Error: File has been modified since last read. "
               "Re-read the file before editing to avoid overwriting external changes. "
               "File: " + path.string();
    }

    // Read file (save original for diff)
    std::ifstream inFile(path);
    std::stringstream buffer;
    buffer << inFile.rdbuf();
    String originalContent = buffer.str();
    inFile.close();

    String content = originalContent;

    // Execute replacement
    size_t pos = 0;
    int count = 0;

    if (replaceAll) {
        while ((pos = content.find(oldString, pos)) != String::npos) {
            content.replace(pos, oldString.length(), newString);
            pos += newString.length();
            count++;
        }
    } else {
        pos = content.find(oldString);
        if (pos == String::npos) {
            return "Error: String not found in file:\n" + oldString;
        }
        content.replace(pos, oldString.length(), newString);
        count = 1;
    }

    // Generate diff
    String diff = generateDiff(originalContent, content, path.string(), oldString, newString);

    // Write back
    std::ofstream outFile(path);
    outFile << content;

    // Invalidate cache + update mtime
    FileCache::instance().invalidate(path.string());

    // Build result with diff
    std::ostringstream result;
    result << "Successfully replaced " << count << " occurrence(s) in " << path.string() << "\n\n";
    result << diff;
    return result.str();
}

} // namespace claude
