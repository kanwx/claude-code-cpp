#include <claude/tool/impl/FileWriteTool.hpp>
#include <claude/utils/FileCache.hpp>
#include <fstream>

namespace claude {

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

    std::ofstream file(path);
    if (!file) {
        return "Error: Cannot write to file: " + path.string();
    }

    file << content;

    // Update file cache after successful write
    FileCache::instance().invalidate(path.string());

    return "Successfully wrote to " + path.string() +
           " (" + std::to_string(content.size()) + " bytes)";
}

} // namespace claude
