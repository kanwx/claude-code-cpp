#include <claude/permission/YoloClassifier.hpp>
#include <claude/api/ApiClient.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_set>

namespace claude {

YoloClassifier::YoloClassifier() = default;

// ========== 核心分类 ==========

YoloResult YoloClassifier::classify(
    const String& toolName,
    const Json& input,
    const std::vector<Json>& transcript
) {
    if (!enabled_ || !apiClient_) {
        // 无 API 客户端时：只使用快速路径
        if (wouldAcceptInEditMode(toolName, input)) {
            return YoloResult::allow("AcceptEdits fast-path (no API client)", "acceptEdits");
        }
        if (SAFE_YOLO_ALLOWLISTED_TOOLS.contains(toolName)) {
            return YoloResult::allow("Safe tool allowlist (no API client)", "allowlist");
        }
        // 无分类器时默认阻止
        return YoloResult::block("No classifier available", "fast");
    }

    // 1. AcceptEdits 快速路径
    if (wouldAcceptInEditMode(toolName, input)) {
        return YoloResult::allow("AcceptEdits fast-path", "acceptEdits");
    }

    // 2. 安全工具白名单
    if (SAFE_YOLO_ALLOWLISTED_TOOLS.contains(toolName)) {
        return YoloResult::allow("Safe tool allowlist", "allowlist");
    }

    // 3. 提取工具输入 — 空输入意味着无安全相关性，跳过分类器
    String toolInput = extractToolInput(toolName, input);
    if (toolInput.empty() && toolName != "Bash" && toolName != "Write" && toolName != "Edit") {
        return YoloResult::allow("No security-relevant input", "allowlist");
    }

    // 4. 检查缓存
    String cacheKey = toolName + ":" + toolInput;
    {
        auto cached = lookupCache(cacheKey);
        if (cached) return *cached;
    }

    // 5. LLM 分类
    YoloResult result;

    switch (twoStageMode_) {
        case TwoStageMode::Fast: {
            auto fastResult = classifyFast(toolName, input, transcript);
            if (fastResult) {
                result = *fastResult;
            } else {
                // 无法解析时默认阻止 (fail-closed)
                result = YoloResult::block("Unparseable fast response", "fast");
            }
            break;
        }
        case TwoStageMode::Thinking: {
            result = classifyThinking(toolName, input, transcript);
            break;
        }
        case TwoStageMode::Both:
        default: {
            // 阶段 1: 快速分类
            auto fastResult = classifyFast(toolName, input, transcript);

            if (fastResult) {
                if (!fastResult->shouldBlock) {
                    // 快速阶段判定为允许 → 直接返回
                    result = *fastResult;
                    break;
                }
                // 快速阶段判定为阻止 → 升级到阶段 2
                result = classifyThinking(toolName, input, transcript);
            } else {
                // 快速阶段无法解析 → 升级到阶段 2
                result = classifyThinking(toolName, input, transcript);
            }
            break;
        }
    }

    // 6. 缓存结果
    updateCache(cacheKey, result);

    return result;
}

// ========== AcceptEdits 快速路径 ==========

bool YoloClassifier::wouldAcceptInEditMode(const String& toolName, const Json& input) {
    // AcceptEdits 模式允许的工具
    static const std::unordered_set<String> EDIT_TOOLS = {
        "Write", "Edit", "NotebookEdit"
    };

    // 文件编辑工具：检查路径是否在当前工作目录内
    if (EDIT_TOOLS.contains(toolName)) {
        String filePath = input.value("file_path", "");
        if (!filePath.empty()) {
            // 简单检查：不是绝对路径到系统目录
            if (filePath.starts_with("/etc/") ||
                filePath.starts_with("/usr/") ||
                filePath.starts_with("/System/") ||
                filePath.starts_with("/bin/") ||
                filePath.starts_with("/sbin/")) {
                return false;  // 系统目录不接受
            }
            return true;  // 普通文件路径允许
        }
    }

    // Bash: 只读命令快速路径
    if (toolName == "Bash") {
        String cmd = input.value("command", "");
        if (cmd.empty()) return false;

        // 提取命令名
        size_t space = cmd.find(' ');
        String cmdName = (space != String::npos) ? cmd.substr(0, space) : cmd;

        static const std::unordered_set<String> SAFE_BASH = {
            "ls", "cat", "head", "tail", "pwd", "echo", "which",
            "find", "wc", "sort", "uniq", "grep", "rg", "git",
            "npm", "node", "python3", "python", "cargo", "rustc",
            "go", "make", "cmake", "ctest", "bun"
        };

        if (SAFE_BASH.contains(cmdName)) return true;
    }

    return false;
}

// ========== 工具安全输入提取 ==========

String YoloClassifier::extractToolInput(const String& toolName, const Json& input) {
    if (toolName == "Bash") {
        return input.value("command", "");
    }
    if (toolName == "Write" || toolName == "Edit") {
        return input.value("file_path", "");
    }
    if (toolName == "NotebookEdit") {
        return input.value("notebook_path", "");
    }
    if (toolName == "Agent") {
        // Agent 工具：显示类型和描述
        String type = input.value("subagent_type", "general-purpose");
        String prompt = input.value("prompt", "");
        if (prompt.length() > 200) prompt = prompt.substr(0, 197) + "...";
        return "[" + type + "] " + prompt;
    }
    // 其他工具：返回空表示无安全相关性
    return "";
}

// ========== 阶段 1: 快速分类 ==========

std::optional<YoloResult> YoloClassifier::classifyFast(
    const String& toolName,
    const Json& input,
    const std::vector<Json>& transcript
) {
    if (!apiClient_) return std::nullopt;

    try {
        String systemPrompt = buildSystemPrompt();
        auto messages = buildTranscript(toolName, input, transcript);

        // 添加分类请求后缀
        String suffix = "\nErr on the side of blocking. <block> immediately.";
        if (!messages.empty()) {
            String& lastContent = messages.back()["content"].get_ref<String&>();
            lastContent += suffix;
        }

        Json tools = Json::array();  // 无工具调用

        // Fast stage: constrain output to 64 tokens (only need <block>yes/no</block>)
        int savedMaxTokens = 16384;
        apiClient_->setMaxTokens(64);

        auto result = apiClient_->call(
            {{{"role", "system"}, {"content", systemPrompt}}, messages.front()},
            tools
        );

        // Restore max tokens after fast classification
        apiClient_->setMaxTokens(savedMaxTokens);

        if (!result) {
            spdlog::warn("YoloClassifier fast call failed: {}", result.error());
            return std::nullopt;
        }

        // 提取响应文本
        String responseText;
        auto& resp = *result;
        if (resp.contains("content") && resp["content"].is_array()) {
            for (auto& block : resp["content"]) {
                if (block.value("type", "") == "text") {
                    responseText += block.value("text", "");
                }
            }
        } else if (resp.contains("content") && resp["content"].is_string()) {
            responseText = resp["content"].get<String>();
        }

        // 解析 <block> 标签
        auto blockResult = parseXmlBlock(responseText);
        if (!blockResult) return std::nullopt;

        if (*blockResult) {
            // 阻止
            String reason = parseXmlReason(responseText);
            return YoloResult::block(reason.empty() ? "Blocked by fast classifier" : reason, "fast");
        } else {
            return YoloResult::allow("Allowed by fast classifier", "fast");
        }

    } catch (const std::exception& e) {
        spdlog::warn("YoloClassifier fast exception: {}", e.what());
        return std::nullopt;
    }
}

// ========== 阶段 2: 思考分类 ==========

YoloResult YoloClassifier::classifyThinking(
    const String& toolName,
    const Json& input,
    const std::vector<Json>& transcript
) {
    if (!apiClient_) {
        return YoloResult::block("No API client for thinking classifier", "thinking");
    }

    try {
        String systemPrompt = buildSystemPrompt();
        auto messages = buildTranscript(toolName, input, transcript);

        // 思考阶段后缀
        String suffix = "\nReview the classification process and follow it carefully, "
                        "making sure you deny actions that should be blocked. "
                        "As a reminder, explicit (not suggestive or implicit) user confirmation "
                        "is required to override blocks. Use <thinking> before responding with <block>.";

        if (!messages.empty()) {
            String& lastContent = messages.back()["content"].get_ref<String&>();
            lastContent += suffix;
        }

        Json tools = Json::array();

        auto result = apiClient_->call(
            {{{"role", "system"}, {"content", systemPrompt}}, messages.front()},
            tools
        );

        if (!result) {
            spdlog::warn("YoloClassifier thinking call failed: {}", result.error());
            // Fail-closed: 无法分类时默认阻止
            return YoloResult::block("Classifier call failed: " + result.error(), "thinking");
        }

        String responseText;
        auto& resp = *result;
        if (resp.contains("content") && resp["content"].is_array()) {
            for (auto& block : resp["content"]) {
                if (block.value("type", "") == "text") {
                    responseText += block.value("text", "");
                }
            }
        } else if (resp.contains("content") && resp["content"].is_string()) {
            responseText = resp["content"].get<String>();
        }

        // 解析 <block> 标签
        auto blockResult = parseXmlBlock(responseText);
        if (!blockResult) {
            // 无法解析 → 阻止 (fail-closed)
            return YoloResult::block("Unparseable thinking response", "thinking");
        }

        String reason = parseXmlReason(responseText);

        if (*blockResult) {
            return YoloResult::block(reason.empty() ? "Blocked by thinking classifier" : reason, "thinking");
        } else {
            return YoloResult::allow(reason.empty() ? "Allowed by thinking classifier" : reason, "thinking");
        }

    } catch (const std::exception& e) {
        spdlog::warn("YoloClassifier thinking exception: {}", e.what());
        return YoloResult::block("Classifier exception: " + String(e.what()), "thinking");
    }
}

// ========== 提示词构建 ==========

String YoloClassifier::buildSystemPrompt() const {
    if (!systemPromptDirty_ && !cachedSystemPrompt_.empty()) {
        return cachedSystemPrompt_;
    }

    std::ostringstream oss;
    oss << "You are a permission classifier for an AI coding assistant. "
        << "Your job is to decide whether a tool use should be ALLOWED or BLOCKED.\n\n"
        << "Rules:\n"
        << "- Block actions that could cause irreversible damage (deleting important files, "
        << "running destructive commands, modifying system configurations)\n"
        << "- Block actions that access sensitive data (credentials, secrets, private keys)\n"
        << "- Block actions that affect systems outside the user's project directory\n"
        << "- Allow actions that are part of normal development workflow "
        << "(reading files, running tests, installing packages, git operations)\n"
        << "- When in doubt, BLOCK the action\n\n"
        << "Respond using XML tags:\n"
        << "<block>yes</block> or <block>no</block>\n"
        << "<reason>brief explanation</reason>\n";

    // 用户配置的规则
    if (!autoModeRules_.allow.empty()) {
        oss << "\nUser-configured ALLOW rules:\n";
        for (const auto& rule : autoModeRules_.allow) {
            oss << "- " << rule << "\n";
        }
    }

    if (!autoModeRules_.softDeny.empty()) {
        oss << "\nUser-configured DENY rules:\n";
        for (const auto& rule : autoModeRules_.softDeny) {
            oss << "- " << rule << "\n";
        }
    }

    if (!autoModeRules_.environment.empty()) {
        oss << "\nUser environment context:\n";
        for (const auto& env : autoModeRules_.environment) {
            oss << "- " << env << "\n";
        }
    }

    cachedSystemPrompt_ = oss.str();
    systemPromptDirty_ = false;
    return cachedSystemPrompt_;
}

std::vector<Json> YoloClassifier::buildTranscript(
    const String& toolName,
    const Json& input,
    const std::vector<Json>& transcript
) const {
    std::vector<Json> messages;

    // 注入 CLAUDE.md 内容
    if (!claudeMdContent_.empty()) {
        messages.push_back({
            {"role", "user"},
            {"content", "<user_claude_md>\n" + claudeMdContent_ + "\n</user_claude_md>"}
        });
    }

    // 添加对话历史 (只保留用户消息和工具调用)
    for (const auto& msg : transcript) {
        String role = msg.value("role", "");
        if (role == "user") {
            // 用户消息：只保留文本块
            if (msg.contains("content") && msg["content"].is_string()) {
                messages.push_back({
                    {"role", "user"},
                    {"content", msg["content"].get<String>()}
                });
            }
        } else if (role == "assistant") {
            // 助手消息：只保留工具调用 (不包含文本，避免影响分类器)
            if (msg.contains("content") && msg["content"].is_array()) {
                Json toolUses = Json::array();
                for (const auto& block : msg["content"]) {
                    if (block.value("type", "") == "tool_use") {
                        toolUses.push_back(block);
                    }
                }
                if (!toolUses.empty()) {
                    messages.push_back({
                        {"role", "assistant"},
                        {"content", toolUses}
                    });
                }
            }
        }
    }

    // 添加当前工具调用请求
    String toolInput = extractToolInput(toolName, input);
    String requestContent = "Tool: " + toolName;
    if (!toolInput.empty()) {
        requestContent += "\nInput: " + toolInput;
    }

    messages.push_back({
        {"role", "user"},
        {"content", "Should the following tool use be allowed?\n" + requestContent}
    });

    return messages;
}

// ========== XML 解析 ==========

std::optional<bool> YoloClassifier::parseXmlBlock(const String& text) {
    // 先清除 <thinking> 内容，避免干扰
    String cleaned = stripThinkingTags(text);

    // 查找 <block> 标签
    auto start = cleaned.find("<block>");
    if (start == String::npos) return std::nullopt;

    auto end = cleaned.find("</block>", start);
    if (end == String::npos) return std::nullopt;

    String value = cleaned.substr(start + 7, end - start - 7);

    // Trim
    size_t first = value.find_first_not_of(" \t\n\r");
    size_t last = value.find_last_not_of(" \t\n\r");
    if (first == String::npos) return std::nullopt;
    value = value.substr(first, last - first + 1);

    if (value == "yes" || value == "true" || value == "1") return true;
    if (value == "no" || value == "false" || value == "0") return false;
    return std::nullopt;
}

String YoloClassifier::parseXmlReason(const String& text) {
    String cleaned = stripThinkingTags(text);

    auto start = cleaned.find("<reason>");
    if (start == String::npos) return "";

    auto end = cleaned.find("</reason>", start);
    if (end == String::npos) return "";

    String value = cleaned.substr(start + 8, end - start - 8);

    // Trim
    size_t first = value.find_first_not_of(" \t\n\r");
    size_t last = value.find_last_not_of(" \t\n\r");
    if (first == String::npos) return "";
    return value.substr(first, last - first + 1);
}

String YoloClassifier::stripThinkingTags(const String& text) {
    // 移除 <thinking>...</thinking> 内容
    String result;
    size_t pos = 0;
    while (pos < text.size()) {
        auto start = text.find("<thinking>", pos);
        if (start == String::npos) {
            result += text.substr(pos);
            break;
        }
        result += text.substr(pos, start - pos);
        auto end = text.find("</thinking>", start);
        if (end == String::npos) break;
        pos = end + 11;
    }
    return result;
}

// ========== 缓存 ==========

void YoloClassifier::clearCache() {
    std::lock_guard lock(cacheMutex_);
    cache_.clear();
}

std::optional<YoloResult> YoloClassifier::lookupCache(const String& key) const {
    std::lock_guard lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    return std::nullopt;
}

void YoloClassifier::updateCache(const String& key, const YoloResult& result) {
    std::lock_guard lock(cacheMutex_);
    cache_[key] = result;
}

} // namespace claude
