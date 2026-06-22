#include <claude/tool/impl/GrepTool.hpp>
#include <fstream>
#include <regex>
#include <cstdio>
#include <array>
#include <spdlog/spdlog.h>

namespace claude {

String GrepTool::cachedRgPath_;
bool GrepTool::forceUseFallbackForTest = false;
String GrepTool::ripgrepPathOverride;

String GrepTool::inputSchema() const {
    return R"schema({"type":"object","properties":{"pattern":{"type":"string","description":"The regular expression pattern to search for"},"path":{"type":"string","description":"The directory to search in"},"glob":{"type":"string","description":"Glob pattern to filter files"},"type":{"type":"string","description":"File type filter (e.g. js, py, rust, go, java)"},"output_mode":{"type":"string","enum":["content","files_with_matches","count"],"description":"Output format"},"head_limit":{"type":"integer","description":"Limit output to first N results (default 250)"}},"required":["pattern"]})schema";
}

bool GrepTool::hasRipgrep() {
    if (forceUseFallbackForTest) return false;

    if (!ripgrepPathOverride.empty()) {
        if (std::filesystem::exists(ripgrepPathOverride)) {
            cachedRgPath_ = ripgrepPathOverride;
            spdlog::debug("Found ripgrep (override) at: {}", cachedRgPath_);
            return true;
        }
        return false;
    }

    if (!cachedRgPath_.empty()) return true;

    // 尝试常见路径（按优先级）
    static const char* commonPaths[] = {
        "/opt/homebrew/bin/rg",       // Homebrew ARM Mac
        "/usr/local/bin/rg",          // Homebrew Intel Mac / Linux
        "/usr/bin/rg",                // Linux package
        nullptr
    };
    for (const char** p = commonPaths; *p; ++p) {
        if (std::filesystem::exists(*p)) {
            cachedRgPath_ = *p;
            spdlog::debug("Found ripgrep at: {}", cachedRgPath_);
            return true;
        }
    }

    // 尝试 which/whereis
    {
        auto* pipe = popen("which rg 2>/dev/null", "r");
        if (pipe) {
            std::array<char, 256> buf;
            if (fgets(buf.data(), buf.size(), pipe)) {
                cachedRgPath_ = buf.data();
                // 去尾换行
                while (!cachedRgPath_.empty() && (cachedRgPath_.back() == '\n' || cachedRgPath_.back() == '\r'))
                    cachedRgPath_.pop_back();
                pclose(pipe);
                if (std::filesystem::exists(cachedRgPath_)) {
                    spdlog::debug("Found ripgrep at: {}", cachedRgPath_);
                    return true;
                }
                cachedRgPath_.clear();
            } else {
                pclose(pipe);
            }
        }
    }

    return false;
}

String GrepTool::execute(const Json& input, ToolContext& context) {
    if (hasRipgrep()) {
        return executeWithRipgrep(input, context);
    }
    return executeWithRegex(input, context);
}

String GrepTool::executeWithRipgrep(const Json& input, ToolContext& context) {
    String pattern = input["pattern"];
    String searchPath = input.value("path", context.workDir.string());
    String glob = input.value("glob", "");
    String type = input.value("type", "");
    String outputMode = input.value("output_mode", "content");
    int headLimit = input.value("head_limit", 250);

    // 构建 rg 命令
    std::ostringstream cmd;
    cmd << cachedRgPath_;

    // 输出格式
    if (outputMode == "files_with_matches") {
        cmd << " -l";  // 只输出文件名
    } else if (outputMode == "count") {
        cmd << " -c";  // 每文件匹配计数
    } else {
        cmd << " -n";  // 行号
    }

    // head_limit: 使用 --max-count 限制每个文件的匹配数
    if (headLimit > 0) {
        cmd << " -m " << headLimit;
    }

    // glob 过滤
    if (!glob.empty()) {
        cmd << " --glob '" << glob << "'";
    }

    // type 过滤
    if (!type.empty()) {
        cmd << " --type " << type;
    }

    // 排除常见目录
    cmd << " --glob '!.git' --glob '!node_modules' --glob '!.svn'";

    // 忽略大小写提示 (默认大小写敏感)
    // pattern 和 path
    // 转义 shell 特殊字符
    String escapedPattern = pattern;
    String escapedPath = searchPath;

    cmd << " -- '" << escapedPattern << "' '" << escapedPath << "'";

    cmd << " 2>/dev/null";  // 忽略 stderr (权限错误等)

    spdlog::debug("Running ripgrep: {}", cmd.str());

    // 执行命令
    auto* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        spdlog::warn("Failed to run ripgrep, falling back to std::regex");
        return executeWithRegex(input, context);
    }

    std::string result;
    std::array<char, 4096> buf;
    int lineCount = 0;
    while (fgets(buf.data(), buf.size(), pipe)) {
        result += buf.data();
        lineCount++;
        // 额外的行数限制 (安全网)
        if (outputMode == "content" && lineCount >= headLimit) {
            result += "\n... (truncated at " + std::to_string(headLimit) + " lines)\n";
            break;
        }
    }
    int exitCode = pclose(pipe);

    // rg exit codes: 0=matches, 1=no matches, 2=error
    if (exitCode == 1) {
        return "No matches found.\n";
    }
    if (exitCode == 2) {
        spdlog::warn("ripgrep returned error (exit code 2), falling back to std::regex");
        return executeWithRegex(input, context);
    }

    if (outputMode == "count" && result.empty()) {
        return "0 matches found.\n";
    }

    return result;
}

bool GrepTool::isLiteralPattern(const String& pattern) {
    for (char c : pattern) {
        switch (c) {
            case '^': case '$': case '\\': case '.': case '*': case '+':
            case '?': case '(': case ')': case '[': case ']': case '{':
            case '}': case '|':
                return false;
            default: break;
        }
    }
    return true;
}

bool GrepTool::isRegexPatternSafe(const String& pattern) {
    // 1. Length guard: excessively long patterns can cause issues
    if (pattern.size() > 500) return false;

    // 2. Detect nested quantifier groups: (.+)+, (.*)+, (a+)+, ([^x]+)+, etc.
    //    Simple scan for ") +" or ") *" or ")+" or ")*" patterns
    for (size_t i = 0; i + 1 < pattern.size(); ++i) {
        if (pattern[i] == ')' && i + 1 < pattern.size()) {
            char next = pattern[i + 1];
            if (next == '+' || next == '*') return false;
            // also check ")  +" with whitespace
            size_t j = i + 1;
            while (j < pattern.size() && pattern[j] == ' ') ++j;
            if (j < pattern.size() && (pattern[j] == '+' || pattern[j] == '*')) return false;
        }
    }

    // 3. Repeated wildcard: .*.* or .+.+
    if (pattern.find(".*.*") != String::npos) return false;
    if (pattern.find(".+.+") != String::npos) return false;

    // 4. Excessive alternation branches can cause state explosion
    int pipeCount = 0;
    for (char c : pattern) if (c == '|') pipeCount++;
    if (pipeCount > 50) return false;

    return true;
}

String GrepTool::executeWithRegex(const Json& input, ToolContext& context) {
    String pattern = input["pattern"];
    String searchPath = input.value("path", context.workDir.string());
    String glob = input.value("glob", "");
    String outputMode = input.value("output_mode", "content");
    int headLimit = input.value("head_limit", 250);

    // ---- Safety gate: reject dangerous patterns when rg is unavailable ----
    if (!isRegexPatternSafe(pattern)) {
        return "Grep pattern is too complex for the built-in fallback regex engine. "
               "Please install ripgrep (`brew install ripgrep` / `apt install ripgrep`) "
               "or simplify the search pattern (avoid nested quantifiers like (.+)+ "
               "or .*.*).";
    }

    // ---- Literal fast path: avoid std::regex entirely ----
    bool useLiteral = isLiteralPattern(pattern);

    std::filesystem::path basePath(searchPath);
    std::unique_ptr<std::regex> re;
    if (!useLiteral) {
        try {
            re = std::make_unique<std::regex>(pattern, std::regex::ECMAScript);
        } catch (const std::regex_error& e) {
            return String("Regex error: ") + e.what() + "\n";
        }
    }

    // ---- Input size limits ----
    static constexpr size_t MAX_LINE_BYTES = 16 * 1024;      // 16KB per line
    static constexpr int    MAX_FILES_SCANNED = 5000;
    static constexpr size_t MAX_BYTES_SCANNED = 100 * 1024 * 1024; // 100MB
    static constexpr double OVERALL_DEADLINE_SEC = 10.0;

    auto deadline = std::chrono::steady_clock::now()
        + std::chrono::duration<double>(OVERALL_DEADLINE_SEC);

    std::stringstream output;
    int matchCount = 0;
    int fileCount = 0;
    int filesScanned = 0;
    size_t bytesScanned = 0;
    bool deadlineExceeded = false;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(basePath)) {
        if (!entry.is_regular_file()) continue;

        // ---- File count limit ----
        if (filesScanned >= MAX_FILES_SCANNED) {
            output << "... (scanned " << filesScanned << " files, limit reached)\n";
            break;
        }

        // ---- Overall deadline check (between files only) ----
        if (std::chrono::steady_clock::now() > deadline) {
            deadlineExceeded = true;
            output << "... (search timeout after " << OVERALL_DEADLINE_SEC << "s)\n";
            break;
        }

        // 跳过隐藏文件和常见排除目录
        String pathStr = entry.path().string();
        if (pathStr.find("/.git/") != String::npos ||
            pathStr.find("/node_modules/") != String::npos) {
            continue;
        }

        // glob 过滤 (简单实现)
        if (!glob.empty()) {
            String filename = entry.path().filename().string();
            // 仅支持简单 *.ext 格式
            if (glob.find('*') != String::npos) {
                // 简单通配符匹配
                size_t starPos = glob.find('*');
                String prefix = glob.substr(0, starPos);
                String suffix = glob.substr(starPos + 1);
                if (!prefix.empty() && filename.substr(0, prefix.size()) != prefix) continue;
                if (!suffix.empty() && filename.size() >= suffix.size() &&
                    filename.substr(filename.size() - suffix.size()) != suffix) continue;
            } else if (filename != glob) {
                continue;
            }
        }

        std::ifstream file(entry.path());
        if (!file) continue;

        filesScanned++;

        String line;
        int lineNum = 0;
        bool fileHasMatch = false;

        while (std::getline(file, line)) {
            lineNum++;

            // Skip lines that are too long (cannot safely regex-search)
            if (line.size() > MAX_LINE_BYTES) continue;

            bytesScanned += line.size();
            if (bytesScanned > MAX_BYTES_SCANNED) {
                output << "... (scanned ~" << (bytesScanned >> 20) << "MB, limit reached)\n";
                goto done;
            }

            try {
                bool matched = useLiteral
                    ? (line.find(pattern) != String::npos)
                    : std::regex_search(line, *re);

                if (matched) {
                    matchCount++;
                    if (!fileHasMatch) {
                        fileHasMatch = true;
                        fileCount++;
                        if (outputMode == "files_with_matches") {
                            output << entry.path().string() << "\n";
                        }
                    }

                    if (outputMode == "content") {
                        output << entry.path().string() << ":" << lineNum << ": " << line << "\n";
                    }

                    if (matchCount >= headLimit) {
                        output << "\n... (truncated at " << headLimit << " matches)\n";
                        goto done;
                    }
                }
            } catch (const std::regex_error& e) {
                output << "Regex error: " << e.what() << "\n";
                goto done;
            }
        }
    }
    done:

    if (outputMode == "count") {
        output << "Found " << matchCount << " matches in " << fileCount << " files.\n";
    }

    if (output.str().empty() && matchCount == 0 && !deadlineExceeded) {
        return "No matches found.\n";
    }

    return output.str();
}

namespace {

int parseMatchCount(const String& result) {
    // Try to extract count from "Found N matching lines" or "Found N matching files"
    std::regex countRegex(R"(Found (\d+) matching)");
    std::smatch match;
    if (std::regex_search(result, match, countRegex)) {
        return std::stoi(match[1].str());
    }
    // Fallback: count non-empty lines that look like grep output (filepath:line:content)
    int count = 0;
    std::istringstream stream(result);
    String line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.find("matching") == String::npos
            && line.find("Files matching") == String::npos) count++;
    }
    return count;
}

} // anonymous namespace

ToolResultSummary GrepTool::renderToolResult(const String& result, bool isError,
                                  bool isCancelled, bool isRejected) const {
    if (isError) return ToolResultSummary::error("Error searching files");
    if (isCancelled || isRejected) return ToolResultSummary{};
    int matches = parseMatchCount(result);
    if (matches == 0) return ToolResultSummary::dim("No matches found");
    return ToolResultSummary::success("Found " + std::to_string(matches) + " matches", /*bold=*/true, "", "[Ctrl+O to expand]");
}

} // namespace claude
