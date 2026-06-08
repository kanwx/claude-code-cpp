#include <claude/tool/impl/GlobTool.hpp>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <sstream>

namespace claude {

String GlobTool::execute(const Json& input, ToolContext& context) {
    String pattern = input["pattern"];
    String searchPath = input.value("path", context.workDir.string());

    std::filesystem::path basePath(searchPath);

    std::stringstream output;
    output << "Files matching '" << pattern << "' in " << searchPath << ":\n\n";

    // 检查是否是路径模式 (包含 / 或 **)
    bool isPathPattern = pattern.find('/') != String::npos ||
                         pattern.find("**") != String::npos;

    std::vector<std::filesystem::path> matches;
    int count = 0;

    try {
        if (isPathPattern) {
            // 路径模式匹配
            matches = matchPathPattern(basePath, pattern);
        } else {
            // 简单文件名模式
            for (const auto& entry : std::filesystem::recursive_directory_iterator(basePath)) {
                if (entry.is_regular_file()) {
                    String filename = entry.path().filename().string();
                    if (matchesGlob(filename, pattern)) {
                        matches.push_back(entry.path());
                    }
                }
            }
        }

        // 排序结果
        std::sort(matches.begin(), matches.end());

        for (const auto& path : matches) {
            output << path.string() << "\n";
            count++;
            if (count >= 100) {
                output << "\n... (truncated at 100 results)\n";
                break;
            }
        }
    } catch (const std::exception& e) {
        output << "Error: " << e.what() << "\n";
    }

    output << "\nFound " << count << " matching files.";
    return output.str();
}

std::vector<std::filesystem::path> GlobTool::matchPathPattern(
    const std::filesystem::path& basePath, const String& pattern) {

    std::vector<std::filesystem::path> result;

    // 将 glob 模式转换为正则表达式
    String regexPattern = globToRegex(pattern);
    std::regex re(regexPattern, std::regex::extended);

    // 分离目录部分和文件名部分
    size_t lastSlash = pattern.find_last_of('/');
    String dirPattern = lastSlash != String::npos ? pattern.substr(0, lastSlash) : "";
    String filePattern = lastSlash != String::npos ? pattern.substr(lastSlash + 1) : pattern;

    bool hasDoubleStar = pattern.find("**") != String::npos;

    if (hasDoubleStar) {
        // ** 匹配任意深度的目录
        for (const auto& entry : std::filesystem::recursive_directory_iterator(basePath)) {
            if (entry.is_regular_file()) {
                String relativePath = std::filesystem::relative(entry.path(), basePath).string();
                if (std::regex_match(relativePath, re)) {
                    result.push_back(entry.path());
                }
            }
        }
    } else {
        // 单层目录模式
        for (const auto& entry : std::filesystem::directory_iterator(basePath)) {
            if (entry.is_regular_file()) {
                String filename = entry.path().filename().string();
                if (matchesGlob(filename, filePattern)) {
                    result.push_back(entry.path());
                }
            }
        }
    }

    return result;
}

String GlobTool::globToRegex(const String& pattern) {
    String result;
    result.reserve(pattern.size() * 2);

    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];

        switch (c) {
            case '*':
                // 检查是否是 **
                if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
                    result += ".*";
                    ++i;  // 跳过下一个 *
                } else {
                    result += "[^/]*";
                }
                break;
            case '?':
                result += "[^/]";
                break;
            case '.':
                result += "\\.";
                break;
            case '/':
                result += "/";
                break;
            case '[':
                // 字符类
                {
                    size_t end = pattern.find(']', i);
                    if (end != String::npos) {
                        result += pattern.substr(i, end - i + 1);
                        i = end;
                    } else {
                        result += "\\[";
                    }
                }
                break;
            case ']':
                result += "\\]";
                break;
            case '{':
                // 花括号扩展 {a,b,c}
                {
                    size_t end = pattern.find('}', i);
                    if (end != String::npos) {
                        String content = pattern.substr(i + 1, end - i - 1);
                        String altPattern;
                        size_t start = 0;
                        size_t comma = content.find(',');
                        while (comma != String::npos) {
                            if (!altPattern.empty()) altPattern += "|";
                            altPattern += content.substr(start, comma - start);
                            start = comma + 1;
                            comma = content.find(',', start);
                        }
                        if (!altPattern.empty()) altPattern += "|";
                        altPattern += content.substr(start);
                        result += "(" + altPattern + ")";
                        i = end;
                    } else {
                        result += "\\{";
                    }
                }
                break;
            case '}':
                result += "\\}";
                break;
            default:
                if (std::isalnum(c) || c == '_' || c == '-') {
                    result += c;
                } else {
                    result += "\\";
                    result += c;
                }
                break;
        }
    }

    return result;
}

bool GlobTool::matchesGlob(const String& name, const String& pattern) {
    // 支持完整的 glob 模式: *, ?, [], {}

    // 如果包含特殊字符，使用正则表达式
    if (pattern.find_first_of("?[]{}") != String::npos ||
        pattern.find("**") != String::npos) {
        String regexPattern = globToRegex(pattern);
        try {
            std::regex re(regexPattern);
            return std::regex_match(name, re);
        } catch (...) {
            return false;
        }
    }

    // 简单 * 通配符匹配
    if (pattern == "*") return true;

    size_t starPos = pattern.find('*');
    if (starPos == String::npos) {
        return name == pattern;
    }

    String prefix = pattern.substr(0, starPos);
    String suffix = pattern.substr(starPos + 1);

    if (!prefix.empty() && name.substr(0, prefix.length()) != prefix) {
        return false;
    }
    if (!suffix.empty() && name.length() >= suffix.length() &&
        name.substr(name.length() - suffix.length()) != suffix) {
        return false;
    }

    return true;
}

namespace {

int parseFileCount(const String& result) {
    std::regex countRegex(R"(Found (\d+) matching file)");
    std::smatch match;
    if (std::regex_search(result, match, countRegex)) {
        return std::stoi(match[1].str());
    }
    // Fallback: count non-empty, non-header lines
    int count = 0;
    std::istringstream stream(result);
    String line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.find("matching") == String::npos) count++;
    }
    return count;
}

} // anonymous namespace

ToolResultSummary GlobTool::renderToolResult(const String& result, bool isError,
                                  bool isCancelled, bool isRejected) const {
    if (isError) return ToolResultSummary::error("Error searching files");
    if (isCancelled || isRejected) return ToolResultSummary{};
    int files = parseFileCount(result);
    if (files == 0) return ToolResultSummary::dim("No files found");
    return ToolResultSummary::success("Found " + std::to_string(files) + " files", /*bold=*/true, "", "[Ctrl+O to expand]");
}

} // namespace claude
