#include <claude/utils/I18n.hpp>

// Undef convenience macros that conflict with method definitions
#undef tr
#undef trp

#include <cstdlib>
#include <algorithm>

namespace claude {

// ---------------------------------------------------------------------------
// I18n public methods
// ---------------------------------------------------------------------------

I18n& I18n::instance() {
    static I18n i18n;
    return i18n;
}

void I18n::init(Language lang) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (lang == Language::Auto) {
        currentLang_ = detectSystemLanguage();
    } else {
        currentLang_ = lang;
    }

    loadLanguagePack(currentLang_);
}

String I18n::tr(const String& key) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Try current language
    auto it = translations_.find(key);
    if (it != translations_.end()) {
        return it->second;
    }

    // Fallback to English
    auto enIt = englishTranslations_.find(key);
    if (enIt != englishTranslations_.end()) {
        return enIt->second;
    }

    // Return the key itself
    return key;
}

String I18n::tr(const String& key, const std::unordered_map<String, String>& params) const {
    String result = tr(key);
    for (const auto& [k, v] : params) {
        String placeholder = "{" + k + "}";
        size_t pos = result.find(placeholder);
        while (pos != String::npos) {
            result.replace(pos, placeholder.length(), v);
            pos = result.find(placeholder, pos + v.length());
        }
    }
    return result;
}

void I18n::setLanguage(Language lang) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentLang_ = lang;
    loadLanguagePack(lang);
}

I18n::Language I18n::getCurrentLanguage() const {
    return currentLang_;
}

String I18n::getLanguageName() const {
    return languageToString(currentLang_);
}

std::vector<std::pair<I18n::Language, String>> I18n::getSupportedLanguages() {
    return {
        {Language::English, "English"},
        {Language::Chinese, "中文"},
        {Language::Japanese, "日本語"},
        {Language::Korean, "한국어"}
    };
}

// ---------------------------------------------------------------------------
// I18n private methods
// ---------------------------------------------------------------------------

I18n::I18n() {
    // Initialize English translations (default)
    initEnglishTranslations();
}

I18n::Language I18n::detectSystemLanguage() {
    // Check environment variables
    const char* lang = std::getenv("LANG");
    const char* lcAll = std::getenv("LC_ALL");
    const char* langEnv = lang ? lang : (lcAll ? lcAll : "");

    String langStr(langEnv);
    std::transform(langStr.begin(), langStr.end(), langStr.begin(), ::tolower);

    if (langStr.find("zh") != String::npos || langStr.find("chinese") != String::npos) {
        return Language::Chinese;
    }
    if (langStr.find("ja") != String::npos || langStr.find("japanese") != String::npos) {
        return Language::Japanese;
    }
    if (langStr.find("ko") != String::npos || langStr.find("korean") != String::npos) {
        return Language::Korean;
    }

    return Language::English;
}

String I18n::languageToString(Language lang) {
    switch (lang) {
        case Language::English: return "English";
        case Language::Chinese: return "中文";
        case Language::Japanese: return "日本語";
        case Language::Korean: return "한국어";
        default: return "English";
    }
}

void I18n::loadLanguagePack(Language lang) {
    if (lang == Language::English) {
        translations_ = englishTranslations_;
        return;
    }

    // Load built-in translations
    if (lang == Language::Chinese) {
        loadChineseTranslations();
    }
    // Can be extended to load external language files
}

void I18n::initEnglishTranslations() {
    // Commands
    englishTranslations_["cmd.usage"] = "Usage:";
    englishTranslations_["cmd.error"] = "Error:";
    englishTranslations_["cmd.success"] = "Success";
    englishTranslations_["cmd.available"] = "Available";
    englishTranslations_["cmd.not_found"] = "Command not found";

    // Common
    englishTranslations_["common.yes"] = "Yes";
    englishTranslations_["common.no"] = "No";
    englishTranslations_["common.cancel"] = "Cancel";
    englishTranslations_["common.confirm"] = "Confirm";
    englishTranslations_["common.loading"] = "Loading...";
    englishTranslations_["common.processing"] = "Processing...";
    englishTranslations_["common.done"] = "Done";
    englishTranslations_["common.failed"] = "Failed";
    englishTranslations_["common.enabled"] = "Enabled";
    englishTranslations_["common.disabled"] = "Disabled";
    englishTranslations_["common.on"] = "ON";
    englishTranslations_["common.off"] = "OFF";
    englishTranslations_["common.language"] = "Language";

    // Tools
    englishTranslations_["tool.read"] = "Read file";
    englishTranslations_["tool.write"] = "Write file";
    englishTranslations_["tool.edit"] = "Edit file";
    englishTranslations_["tool.bash"] = "Execute command";
    englishTranslations_["tool.glob"] = "Find files";
    englishTranslations_["tool.grep"] = "Search in files";

    // Errors
    englishTranslations_["error.file_not_found"] = "File not found";
    englishTranslations_["error.permission_denied"] = "Permission denied";
    englishTranslations_["error.invalid_input"] = "Invalid input";
    englishTranslations_["error.api_key_missing"] = "API key not configured";
    englishTranslations_["error.network_error"] = "Network error";
    englishTranslations_["error.timeout"] = "Operation timed out";

    // Permissions
    englishTranslations_["permission.allow"] = "Allow";
    englishTranslations_["permission.deny"] = "Deny";
    englishTranslations_["permission.ask"] = "Ask";
    englishTranslations_["permission.always_allow"] = "Always allow";
    englishTranslations_["permission.always_deny"] = "Always deny";

    // Debug
    englishTranslations_["debug.title"] = "=== Debug Mode ===";
    englishTranslations_["debug.stack_trace"] = "Stack Trace";
    englishTranslations_["debug.memory_usage"] = "Memory Usage";
    englishTranslations_["debug.profiling"] = "Profiling";

    // Workflows
    englishTranslations_["workflow.title"] = "=== Workflows ===";
    englishTranslations_["workflow.running"] = "Running workflow";
    englishTranslations_["workflow.complete"] = "Workflow complete";
    englishTranslations_["workflow.step"] = "Step";

    // Advisor
    englishTranslations_["advisor.title"] = "=== AI Advisor ===";
    englishTranslations_["advisor.topics"] = "Available Topics";

    // Status texts (for spinner and thinking)
    englishTranslations_["status.thinking"] = "Thinking...";
    englishTranslations_["status.processing"] = "Processing...";
    englishTranslations_["status.tinkering"] = "Tinkering...";
    englishTranslations_["status.calculating"] = "Calculating...";
    englishTranslations_["status.cooking"] = "Cooking...";
    englishTranslations_["status.done"] = "done";
    englishTranslations_["status.executing"] = "Executing...";
    englishTranslations_["status.reading"] = "Reading...";
    englishTranslations_["status.writing"] = "Writing...";
    englishTranslations_["status.editing"] = "Editing...";
    englishTranslations_["status.searching"] = "Searching...";
    englishTranslations_["status.analyzing"] = "Analyzing...";
    englishTranslations_["status.generating"] = "Generating...";
    englishTranslations_["status.waiting"] = "Waiting...";
    englishTranslations_["status.connecting"] = "Connecting...";
    englishTranslations_["status.syncing"] = "Syncing...";
    englishTranslations_["status.compacting"] = "Compacting...";
    englishTranslations_["status.updating"] = "Updating...";

    // Command descriptions (for /help)
    englishTranslations_["cmd.help.desc"] = "Show this help";
    englishTranslations_["cmd.clear.desc"] = "Clear the conversation";
    englishTranslations_["cmd.clear.success"] = "Conversation cleared.";
    englishTranslations_["cmd.cost.desc"] = "Show token usage and cost";
    englishTranslations_["cmd.cost.title"] = "Token Usage";
    englishTranslations_["cmd.cost.input"] = "Input tokens";
    englishTranslations_["cmd.cost.output"] = "Output tokens";
    englishTranslations_["cmd.cost.total"] = "Total tokens";
    englishTranslations_["cmd.exit.desc"] = "Exit the CLI";
    englishTranslations_["cmd.commit.desc"] = "Create a git commit";
    englishTranslations_["cmd.review.desc"] = "Review code changes";
    englishTranslations_["cmd.compact.desc"] = "Compact conversation context";
    englishTranslations_["cmd.config.desc"] = "Manage configuration";
    englishTranslations_["cmd.memory.desc"] = "Manage persistent memory";
    englishTranslations_["cmd.init.desc"] = "Initialize project";
    englishTranslations_["cmd.doctor.desc"] = "Run diagnostics";
    englishTranslations_["cmd.status.desc"] = "Show session status";
    englishTranslations_["cmd.model.desc"] = "Switch model";
    englishTranslations_["cmd.model.title"] = "Model";
    englishTranslations_["cmd.model.provider"] = "Provider";
    englishTranslations_["cmd.model.current"] = "Current";
    englishTranslations_["cmd.model.aliases"] = "Model aliases";
    englishTranslations_["cmd.model.full_ids"] = "Full model IDs";
    englishTranslations_["cmd.model.examples"] = "Examples";
    englishTranslations_["cmd.model.updated"] = "Model Updated";
    englishTranslations_["cmd.model.note"] = "Note: Changes apply to current session only.";
    englishTranslations_["cmd.permissions.desc"] = "Permission settings";
    englishTranslations_["cmd.skills.desc"] = "List skills";
    englishTranslations_["cmd.hooks.desc"] = "View hooks";
    englishTranslations_["cmd.vim.desc"] = "Toggle vim mode";
    englishTranslations_["cmd.effort.desc"] = "Set effort level";
    englishTranslations_["cmd.theme.desc"] = "Set theme";
    englishTranslations_["cmd.version.desc"] = "Show version";
    englishTranslations_["cmd.bug.desc"] = "Report a bug";
    englishTranslations_["cmd.feedback.desc"] = "Send feedback";
    englishTranslations_["cmd.export.desc"] = "Export session";
    englishTranslations_["cmd.session.desc"] = "Manage sessions";
    englishTranslations_["cmd.login.desc"] = "Login status";
    englishTranslations_["cmd.branch.desc"] = "Git branches";
    englishTranslations_["cmd.tag.desc"] = "Tag state";
    englishTranslations_["cmd.lang.desc"] = "Switch language";

    // Common
    englishTranslations_["common.default"] = "default";

    // Error messages
    englishTranslations_["error.no_session"] = "No active session.";
    englishTranslations_["error.git.required"] = "Not in a git repository";
    englishTranslations_["error.git.branch_required"] = "Branch name required";
    englishTranslations_["error.git.branch_not_found"] = "Branch not found";
    englishTranslations_["error.file.not_found"] = "File not found";
    englishTranslations_["error.file.not_a_file"] = "Not a file";
    englishTranslations_["error.dir.not_found"] = "Directory not found";
    englishTranslations_["error.dir.not_a_dir"] = "Not a directory";
    englishTranslations_["error.url.required"] = "URL required";
    englishTranslations_["error.selector.required"] = "Selector required";
    englishTranslations_["error.text.required"] = "Text required";
    englishTranslations_["error.js.required"] = "JavaScript required";
    englishTranslations_["error.pr.not_found"] = "PR not found";
    englishTranslations_["error.pr.checkout_failed"] = "Could not checkout PR";
    englishTranslations_["error.effort.invalid"] = "Invalid effort level";
    englishTranslations_["error.service.required"] = "Service name required";
    englishTranslations_["error.service.not_found"] = "Service not found";
    englishTranslations_["error.service.not_connected"] = "Not connected to service";
    englishTranslations_["error.webhook.not_configured"] = "No webhook configured";
    englishTranslations_["error.tag.required"] = "Tag name required";
    englishTranslations_["error.tag.not_found"] = "Tag not found";
    englishTranslations_["error.mcp.server_not_found"] = "MCP server not found";
    englishTranslations_["error.mcp.not_connected"] = "Not connected";
    englishTranslations_["error.mcp.no_response"] = "No response";
    englishTranslations_["error.mcp.invalid_response"] = "Invalid response";
    englishTranslations_["error.language.unknown"] = "Unknown language";

    // Success messages
    englishTranslations_["success.connected"] = "Successfully connected";
    englishTranslations_["success.done"] = "Done";
    englishTranslations_["success.not_connected"] = "Not connected";

    // Branch command
    englishTranslations_["cmd.branch.title"] = "Git Branches";
    englishTranslations_["cmd.branch.current"] = "Current";
    englishTranslations_["cmd.branch.all"] = "All branches";
    englishTranslations_["cmd.branch.list_desc"] = "List branches";
    englishTranslations_["cmd.branch.create_desc"] = "Create new branch";
    englishTranslations_["cmd.branch.switch_desc"] = "Switch to branch";
    englishTranslations_["cmd.branch.delete_desc"] = "Delete branch";

    // Diff command
    englishTranslations_["cmd.diff.title"] = "Diff";

    // Chrome command
    englishTranslations_["cmd.chrome.title"] = "Chrome";
    englishTranslations_["cmd.chrome.close_desc"] = "Close browser";
    englishTranslations_["cmd.chrome.screenshot_desc"] = "Take screenshot";
    englishTranslations_["cmd.chrome.status_desc"] = "Connection status";
    englishTranslations_["cmd.chrome.opening"] = "Opening";
    englishTranslations_["cmd.chrome.closing"] = "Closing Chrome";
    englishTranslations_["cmd.chrome.close_note"] = "Note: Closes all Chrome instances";
    englishTranslations_["cmd.chrome.screenshot_saved"] = "Screenshot saved";
    englishTranslations_["cmd.chrome.simulated"] = "simulated - requires Chrome DevTools";
    englishTranslations_["cmd.chrome.clicking"] = "Clicking";
    englishTranslations_["cmd.chrome.typing"] = "Typing";
    englishTranslations_["cmd.chrome.evaluating"] = "Evaluating";
    englishTranslations_["cmd.chrome.result"] = "Result";
    englishTranslations_["cmd.chrome.status"] = "Chrome Status";
    englishTranslations_["cmd.chrome.connection"] = "Connection";
    englishTranslations_["cmd.chrome.start_with"] = "Start Chrome with";
    englishTranslations_["cmd.chrome.requirements"] = "Requirements";

    // Tag command
    englishTranslations_["cmd.tag.title"] = "Tag";

    // MCP command
    englishTranslations_["cmd.mcp.title"] = "MCP";

    // Commands generic
    englishTranslations_["cmd.commands"] = "Commands";
    englishTranslations_["cmd.usage"] = "Usage";
    englishTranslations_["cmd.options"] = "Options";
    englishTranslations_["cmd.examples"] = "Examples";
    englishTranslations_["cmd.example"] = "Example";
    englishTranslations_["cmd.actions"] = "Actions";

    // AddDir command
    englishTranslations_["cmd.add_dir.title"] = "Add Directory to Context";
    englishTranslations_["cmd.add_dir.recursive_desc"] = "Include all subdirectories";
    englishTranslations_["cmd.add_dir.file_types_desc"] = "Filter by file extension";
    englishTranslations_["cmd.add_dir.max_size_desc"] = "Maximum total size in MB";
    englishTranslations_["cmd.add_dir.added"] = "Directory added";
    englishTranslations_["cmd.add_dir.statistics"] = "Statistics";
    englishTranslations_["cmd.add_dir.files"] = "Files";
    englishTranslations_["cmd.add_dir.total_size"] = "Total size";
    englishTranslations_["cmd.add_dir.path"] = "Path";
    englishTranslations_["cmd.add_dir.context_note"] = "The directory contents will be included in context.";
    englishTranslations_["cmd.add_dir.use_context_note"] = "Use /context show to see all added directories.";

    // Effort command
    englishTranslations_["cmd.effort.title"] = "Effort Level";
    englishTranslations_["cmd.effort.current"] = "Current";
    englishTranslations_["cmd.effort.levels"] = "Levels";
    englishTranslations_["cmd.effort.low_desc"] = "Quick responses, minimal reasoning";
    englishTranslations_["cmd.effort.medium_desc"] = "Balanced";
    englishTranslations_["cmd.effort.high_desc"] = "Thorough analysis, detailed reasoning";
    englishTranslations_["cmd.effort.max_desc"] = "Maximum effort, exhaustive analysis";
    englishTranslations_["cmd.effort.valid_levels"] = "Valid levels";
    englishTranslations_["cmd.effort.set"] = "Effort Set";
    englishTranslations_["cmd.effort.level"] = "Level";
    englishTranslations_["cmd.effort.quick_enabled"] = "Quick responses enabled";
    englishTranslations_["cmd.effort.thorough_enabled"] = "Thorough analysis enabled";
    englishTranslations_["cmd.effort.max_enabled"] = "Maximum effort enabled";
    englishTranslations_["cmd.effort.max_note"] = "may use more tokens";

    // Theme command
    englishTranslations_["cmd.theme.current"] = "Current Theme";
    englishTranslations_["cmd.theme.theme"] = "Theme";
    englishTranslations_["cmd.theme.available"] = "Available themes";
    englishTranslations_["cmd.theme.default_desc"] = "Standard colors";
    englishTranslations_["cmd.theme.dark_desc"] = "Dark mode optimized";
    englishTranslations_["cmd.theme.light_desc"] = "Light mode optimized";
    englishTranslations_["cmd.theme.monokai_desc"] = "Monokai-inspired";
    englishTranslations_["cmd.theme.dracula_desc"] = "Dracula-inspired";
    englishTranslations_["cmd.theme.nord_desc"] = "Nord color scheme";
    englishTranslations_["cmd.theme.none_desc"] = "No colors (plain text)";
    englishTranslations_["cmd.theme.use_list"] = "Use /theme list to see available themes.";
    englishTranslations_["cmd.theme.changed"] = "Theme Changed";
    englishTranslations_["cmd.theme.colors_disabled"] = "Colors disabled";
    englishTranslations_["cmd.theme.applied"] = "Theme applied to current session";
    englishTranslations_["cmd.theme.default_full_desc"] = "Standard color scheme, works in most terminals.";
    englishTranslations_["cmd.theme.dark_full_desc"] = "Optimized for dark terminal backgrounds.";
    englishTranslations_["cmd.theme.light_full_desc"] = "Optimized for light terminal backgrounds.";
    englishTranslations_["cmd.theme.monokai_full_desc"] = "Monokai-inspired color scheme. Good for code highlighting.";
    englishTranslations_["cmd.theme.dracula_full_desc"] = "Dracula-inspired dark theme.";
    englishTranslations_["cmd.theme.nord_full_desc"] = "Nord color scheme - Arctic, bluish color palette.";
    englishTranslations_["cmd.theme.none_full_desc"] = "No colors - plain text output.";

    // Bridge command
    englishTranslations_["cmd.bridge.title"] = "Bridge Mode";
    englishTranslations_["cmd.bridge.available"] = "Available Bridges";
    englishTranslations_["cmd.bridge.connect_desc"] = "Connect to service";
    englishTranslations_["cmd.bridge.disconnect_desc"] = "Disconnect";
    englishTranslations_["cmd.bridge.status_desc"] = "Show connections";
    englishTranslations_["cmd.bridge.send_desc"] = "Send message";
    englishTranslations_["cmd.bridge.services"] = "Services";
    englishTranslations_["cmd.bridge.connecting"] = "Connecting to";
    englishTranslations_["cmd.bridge.config_required"] = "Configuration required";
    englishTranslations_["cmd.bridge.set_env"] = "Set";
    englishTranslations_["cmd.bridge.then_run"] = "Then run";
    englishTranslations_["cmd.bridge.connection_failed"] = "Connection test failed";
    englishTranslations_["cmd.bridge.disconnected"] = "Disconnected from";
    englishTranslations_["cmd.bridge.connections"] = "Bridge Connections";
    englishTranslations_["cmd.bridge.no_connections"] = "no active connections";
    englishTranslations_["cmd.bridge.to_connect"] = "To connect";
    englishTranslations_["cmd.bridge.set_service_env"] = "Set <service>_WEBHOOK environment variable";
    englishTranslations_["cmd.bridge.run_connect"] = "Run";
    englishTranslations_["cmd.bridge.message_sent"] = "Message sent to";
    englishTranslations_["cmd.bridge.send_failed"] = "Failed to send message to";
    englishTranslations_["cmd.bridge.webhook_sent"] = "Message sent to webhook";
    englishTranslations_["cmd.bridge.webhook_failed"] = "Failed to send to webhook";

    // Diff command
    englishTranslations_["cmd.diff.compare_desc"] = "Compare files";
    englishTranslations_["cmd.diff.git_diff_desc"] = "Compare with staged version (git diff)";
    englishTranslations_["cmd.diff.two_files_desc"] = "Compare two files";
    englishTranslations_["cmd.diff.commit_diff_desc"] = "Compare with older commit";
    englishTranslations_["cmd.diff.file"] = "File";
    englishTranslations_["cmd.diff.to_see_git_diff"] = "To see git diff, run";

    // Tag command
    englishTranslations_["cmd.tag.no_tags"] = "No tags found";
    englishTranslations_["cmd.tag.deleted"] = "Tag deleted";
    englishTranslations_["cmd.tag.created"] = "Tag created";
    englishTranslations_["cmd.tag.description"] = "Description";

    // Additional errors
    englishTranslations_["error.theme.invalid"] = "Invalid theme";
    englishTranslations_["error.action.unknown"] = "Unknown action";
    englishTranslations_["error.service.message_required"] = "Service and message required";
    englishTranslations_["error.url.message_required"] = "URL and message required";
    englishTranslations_["error.git.not_repo"] = "Not in a git repository";
    englishTranslations_["error.git.gh_not_found"] = "gh CLI not found. Install from: https://cli.github.com";
    englishTranslations_["error.git.checkout_failed"] = "Could not checkout PR";
    englishTranslations_["error.git.commit_failed"] = "Commit failed";
    englishTranslations_["error.param.name_required"] = "Name required";
    englishTranslations_["error.param.target_required"] = "Target required";
    englishTranslations_["error.param.file_required"] = "File required";
    englishTranslations_["error.param.title_required"] = "Title required";
    englishTranslations_["error.param.text_required"] = "Text required";
    englishTranslations_["error.param.message_required"] = "Message required";
    englishTranslations_["error.param.value_required"] = "Value required";
    englishTranslations_["error.param.number_required"] = "Number required";
    englishTranslations_["error.param.pr_number_required"] = "PR number required";
    englishTranslations_["error.param.issue_number_required"] = "Issue number required";
    englishTranslations_["error.param.comment_required"] = "Comment required";
    englishTranslations_["error.param.agent_name_required"] = "Agent name required";
    englishTranslations_["error.dir.not_found"] = "Directory not found";
}

void I18n::loadChineseTranslations() {
    translations_ = englishTranslations_; // Copy English as base

    // Commands
    translations_["cmd.usage"] = "用法:";
    translations_["cmd.error"] = "错误:";
    translations_["cmd.success"] = "成功";
    translations_["cmd.available"] = "可用";
    translations_["cmd.not_found"] = "命令未找到";

    // Common
    translations_["common.yes"] = "是";
    translations_["common.no"] = "否";
    translations_["common.cancel"] = "取消";
    translations_["common.confirm"] = "确认";
    translations_["common.loading"] = "加载中...";
    translations_["common.processing"] = "处理中...";
    translations_["common.done"] = "完成";
    translations_["common.failed"] = "失败";
    translations_["common.enabled"] = "已启用";
    translations_["common.disabled"] = "已禁用";
    translations_["common.on"] = "开";
    translations_["common.off"] = "关";

    // Tools
    translations_["tool.read"] = "读取文件";
    translations_["tool.write"] = "写入文件";
    translations_["tool.edit"] = "编辑文件";
    translations_["tool.bash"] = "执行命令";
    translations_["tool.glob"] = "查找文件";
    translations_["tool.grep"] = "搜索文件";

    // Errors
    translations_["error.file_not_found"] = "文件未找到";
    translations_["error.permission_denied"] = "权限不足";
    translations_["error.invalid_input"] = "输入无效";
    translations_["error.api_key_missing"] = "API 密钥未配置";
    translations_["error.network_error"] = "网络错误";
    translations_["error.timeout"] = "操作超时";

    // Permissions
    translations_["permission.allow"] = "允许";
    translations_["permission.deny"] = "拒绝";
    translations_["permission.ask"] = "询问";
    translations_["permission.always_allow"] = "始终允许";
    translations_["permission.always_deny"] = "始终拒绝";

    // Debug
    translations_["debug.title"] = "=== 调试模式 ===";
    translations_["debug.stack_trace"] = "堆栈跟踪";
    translations_["debug.memory_usage"] = "内存使用";
    translations_["debug.profiling"] = "性能分析";

    // Workflows
    translations_["workflow.title"] = "=== 工作流 ===";
    translations_["workflow.running"] = "正在运行工作流";
    translations_["workflow.complete"] = "工作流完成";
    translations_["workflow.step"] = "步骤";

    // Advisor
    translations_["advisor.title"] = "=== AI 顾问 ===";
    translations_["advisor.topics"] = "可用主题";

    // Status texts (for spinner and thinking)
    translations_["status.thinking"] = "思考中...";
    translations_["status.processing"] = "处理中...";
    translations_["status.tinkering"] = "探索中...";
    translations_["status.calculating"] = "计算中...";
    translations_["status.cooking"] = "构建中...";
    translations_["status.done"] = "完成";
    translations_["status.executing"] = "执行中...";
    translations_["status.reading"] = "读取中...";
    translations_["status.writing"] = "写入中...";
    translations_["status.editing"] = "编辑中...";
    translations_["status.searching"] = "搜索中...";
    translations_["status.analyzing"] = "分析中...";
    translations_["status.generating"] = "生成中...";
    translations_["status.waiting"] = "等待中...";
    translations_["status.connecting"] = "连接中...";
    translations_["status.syncing"] = "同步中...";
    translations_["status.compacting"] = "压缩中...";
    translations_["status.updating"] = "更新中...";

    // Command descriptions (for /help)
    translations_["cmd.help.desc"] = "显示帮助信息";
    translations_["cmd.clear.desc"] = "清空对话";
    translations_["cmd.clear.success"] = "对话已清空";
    translations_["cmd.cost.desc"] = "显示 token 使用量和费用";
    translations_["cmd.cost.title"] = "Token 使用量";
    translations_["cmd.cost.input"] = "输入 token";
    translations_["cmd.cost.output"] = "输出 token";
    translations_["cmd.cost.total"] = "总计 token";
    translations_["cmd.exit.desc"] = "退出程序";
    translations_["cmd.commit.desc"] = "创建 git 提交";
    translations_["cmd.review.desc"] = "审查代码变更";
    translations_["cmd.compact.desc"] = "压缩对话上下文";
    translations_["cmd.config.desc"] = "管理配置";
    translations_["cmd.memory.desc"] = "管理持久化内存";
    translations_["cmd.init.desc"] = "初始化项目";
    translations_["cmd.doctor.desc"] = "运行诊断";
    translations_["cmd.status.desc"] = "显示会话状态";
    translations_["cmd.model.desc"] = "切换模型";
    translations_["cmd.model.title"] = "模型";
    translations_["cmd.model.provider"] = "提供商";
    translations_["cmd.model.current"] = "当前";
    translations_["cmd.model.aliases"] = "模型别名";
    translations_["cmd.model.full_ids"] = "完整模型 ID";
    translations_["cmd.model.examples"] = "示例";
    translations_["cmd.model.updated"] = "模型已更新";
    translations_["cmd.model.note"] = "注意：更改仅对当前会话生效。";
    translations_["cmd.permissions.desc"] = "权限设置";
    translations_["cmd.skills.desc"] = "列出技能";
    translations_["cmd.hooks.desc"] = "查看钩子";
    translations_["cmd.vim.desc"] = "切换 vim 模式";
    translations_["cmd.effort.desc"] = "设置努力程度";
    translations_["cmd.theme.desc"] = "设置主题";
    translations_["cmd.version.desc"] = "显示版本";
    translations_["cmd.bug.desc"] = "报告问题";
    translations_["cmd.feedback.desc"] = "发送反馈";
    translations_["cmd.export.desc"] = "导出会话";
    translations_["cmd.session.desc"] = "管理会话";
    translations_["cmd.login.desc"] = "登录状态";
    translations_["cmd.branch.desc"] = "Git 分支";
    translations_["cmd.tag.desc"] = "标签状态";
    translations_["cmd.lang.desc"] = "切换语言";

    // Common
    translations_["common.default"] = "默认";

    // Error messages
    translations_["error.no_session"] = "没有活动会话。";
    translations_["error.git.required"] = "不在 git 仓库中";
    translations_["error.git.branch_required"] = "需要分支名称";
    translations_["error.git.branch_not_found"] = "分支未找到";
    translations_["error.file.not_found"] = "文件未找到";
    translations_["error.file.not_a_file"] = "不是文件";
    translations_["error.dir.not_found"] = "目录未找到";
    translations_["error.dir.not_a_dir"] = "不是目录";
    translations_["error.url.required"] = "需要 URL";
    translations_["error.selector.required"] = "需要选择器";
    translations_["error.text.required"] = "需要文本";
    translations_["error.js.required"] = "需要 JavaScript";
    translations_["error.pr.not_found"] = "PR 未找到";
    translations_["error.pr.checkout_failed"] = "无法检出 PR";
    translations_["error.effort.invalid"] = "无效的努力程度";
    translations_["error.service.required"] = "需要服务名称";
    translations_["error.service.not_found"] = "服务未找到";
    translations_["error.service.not_connected"] = "未连接到服务";
    translations_["error.webhook.not_configured"] = "未配置 webhook";
    translations_["error.tag.required"] = "需要标签名称";
    translations_["error.tag.not_found"] = "标签未找到";
    translations_["error.mcp.server_not_found"] = "MCP 服务器未找到";
    translations_["error.mcp.not_connected"] = "未连接";
    translations_["error.mcp.no_response"] = "无响应";
    translations_["error.mcp.invalid_response"] = "无效响应";
    translations_["error.language.unknown"] = "未知语言";

    // Success messages
    translations_["success.connected"] = "已成功连接";
    translations_["success.done"] = "完成";
    translations_["success.not_connected"] = "未连接";

    // Branch command
    translations_["cmd.branch.title"] = "Git 分支";
    translations_["cmd.branch.current"] = "当前";
    translations_["cmd.branch.all"] = "所有分支";
    translations_["cmd.branch.list_desc"] = "列出分支";
    translations_["cmd.branch.create_desc"] = "创建新分支";
    translations_["cmd.branch.switch_desc"] = "切换分支";
    translations_["cmd.branch.delete_desc"] = "删除分支";

    // Diff command
    translations_["cmd.diff.title"] = "差异对比";

    // Chrome command
    translations_["cmd.chrome.title"] = "Chrome";

    // Tag command
    translations_["cmd.tag.title"] = "标签";

    // MCP command
    translations_["cmd.mcp.title"] = "MCP";

    // Commands generic
    translations_["cmd.commands"] = "命令";
    translations_["cmd.usage"] = "用法";
    translations_["cmd.options"] = "选项";
    translations_["cmd.examples"] = "示例";
    translations_["cmd.example"] = "示例";
    translations_["cmd.actions"] = "操作";

    // AddDir command
    translations_["cmd.add_dir.title"] = "添加目录到上下文";
    translations_["cmd.add_dir.recursive_desc"] = "包含所有子目录";
    translations_["cmd.add_dir.file_types_desc"] = "按文件扩展名过滤";
    translations_["cmd.add_dir.max_size_desc"] = "最大总大小 (MB)";
    translations_["cmd.add_dir.added"] = "目录已添加";
    translations_["cmd.add_dir.statistics"] = "统计信息";
    translations_["cmd.add_dir.files"] = "文件数";
    translations_["cmd.add_dir.total_size"] = "总大小";
    translations_["cmd.add_dir.path"] = "路径";
    translations_["cmd.add_dir.context_note"] = "目录内容将被包含在上下文中。";
    translations_["cmd.add_dir.use_context_note"] = "使用 /context show 查看所有添加的目录。";

    // Effort command
    translations_["cmd.effort.title"] = "努力程度";
    translations_["cmd.effort.current"] = "当前";
    translations_["cmd.effort.levels"] = "级别";
    translations_["cmd.effort.low_desc"] = "快速响应，最少推理";
    translations_["cmd.effort.medium_desc"] = "平衡";
    translations_["cmd.effort.high_desc"] = "详尽分析，详细推理";
    translations_["cmd.effort.max_desc"] = "最大努力，穷尽分析";
    translations_["cmd.effort.valid_levels"] = "有效级别";
    translations_["cmd.effort.set"] = "努力程度已设置";
    translations_["cmd.effort.level"] = "级别";
    translations_["cmd.effort.quick_enabled"] = "快速响应已启用";
    translations_["cmd.effort.thorough_enabled"] = "详尽分析已启用";
    translations_["cmd.effort.max_enabled"] = "最大努力已启用";
    translations_["cmd.effort.max_note"] = "可能使用更多 token";

    // Theme command
    translations_["cmd.theme.current"] = "当前主题";
    translations_["cmd.theme.theme"] = "主题";
    translations_["cmd.theme.available"] = "可用主题";
    translations_["cmd.theme.default_desc"] = "标准颜色";
    translations_["cmd.theme.dark_desc"] = "深色模式优化";
    translations_["cmd.theme.light_desc"] = "浅色模式优化";
    translations_["cmd.theme.monokai_desc"] = "Monokai 风格";

    // Additional error translations
    translations_["error.git.not_repo"] = "不在 git 仓库中";
    translations_["error.git.gh_not_found"] = "未找到 gh CLI。请安装: https://cli.github.com";
    translations_["error.git.checkout_failed"] = "无法检出 PR";
    translations_["error.git.commit_failed"] = "提交失败";
    translations_["error.param.name_required"] = "需要名称";
    translations_["error.param.target_required"] = "需要目标";
    translations_["error.param.file_required"] = "需要文件";
    translations_["error.param.title_required"] = "需要标题";
    translations_["error.param.text_required"] = "需要文本";
    translations_["error.param.message_required"] = "需要消息";
    translations_["error.param.value_required"] = "需要值";
    translations_["error.param.number_required"] = "需要编号";
    translations_["error.param.pr_number_required"] = "需要 PR 编号";
    translations_["error.param.issue_number_required"] = "需要议题编号";
    translations_["error.param.comment_required"] = "需要评论";
    translations_["error.param.agent_name_required"] = "需要智能体名称";
    translations_["error.dir.not_found"] = "目录未找到";
    translations_["cmd.theme.dracula_desc"] = "Dracula 风格";
    translations_["cmd.theme.nord_desc"] = "Nord 配色";
    translations_["cmd.theme.none_desc"] = "无颜色 (纯文本)";
    translations_["cmd.theme.use_list"] = "使用 /theme list 查看可用主题。";
    translations_["cmd.theme.changed"] = "主题已更改";
    translations_["cmd.theme.colors_disabled"] = "颜色已禁用";
    translations_["cmd.theme.applied"] = "主题已应用到当前会话";
    translations_["cmd.theme.default_full_desc"] = "标准配色，适用于大多数终端。";
    translations_["cmd.theme.dark_full_desc"] = "针对深色终端背景优化。";
    translations_["cmd.theme.light_full_desc"] = "针对浅色终端背景优化。";
    translations_["cmd.theme.monokai_full_desc"] = "Monokai 风格配色。适合代码高亮。";
    translations_["cmd.theme.dracula_full_desc"] = "Dracula 风格深色主题。";
    translations_["cmd.theme.nord_full_desc"] = "Nord 配色 - 北极蓝调色板。";
    translations_["cmd.theme.none_full_desc"] = "无颜色 - 纯文本输出。";

    // Bridge command
    translations_["cmd.bridge.title"] = "桥接模式";
    translations_["cmd.bridge.available"] = "可用桥接";
    translations_["cmd.bridge.connect_desc"] = "连接到服务";
    translations_["cmd.bridge.disconnect_desc"] = "断开连接";
    translations_["cmd.bridge.status_desc"] = "显示连接状态";
    translations_["cmd.bridge.send_desc"] = "发送消息";
    translations_["cmd.bridge.services"] = "服务";
    translations_["cmd.bridge.connecting"] = "正在连接";
    translations_["cmd.bridge.config_required"] = "需要配置";
    translations_["cmd.bridge.set_env"] = "设置";
    translations_["cmd.bridge.then_run"] = "然后运行";
    translations_["cmd.bridge.connection_failed"] = "连接测试失败";
    translations_["cmd.bridge.disconnected"] = "已断开";
    translations_["cmd.bridge.connections"] = "桥接连接";
    translations_["cmd.bridge.no_connections"] = "无活动连接";
    translations_["cmd.bridge.to_connect"] = "要连接";
    translations_["cmd.bridge.set_service_env"] = "设置 <服务>_WEBHOOK 环境变量";
    translations_["cmd.bridge.run_connect"] = "运行";
    translations_["cmd.bridge.message_sent"] = "消息已发送到";
    translations_["cmd.bridge.send_failed"] = "发送消息失败到";
    translations_["cmd.bridge.webhook_sent"] = "消息已发送到 webhook";
    translations_["cmd.bridge.webhook_failed"] = "发送到 webhook 失败";

    // Diff command
    translations_["cmd.diff.compare_desc"] = "比较文件";
    translations_["cmd.diff.git_diff_desc"] = "与暂存版本比较 (git diff)";
    translations_["cmd.diff.two_files_desc"] = "比较两个文件";
    translations_["cmd.diff.commit_diff_desc"] = "与旧提交比较";
    translations_["cmd.diff.file"] = "文件";
    translations_["cmd.diff.to_see_git_diff"] = "要查看 git diff，运行";

    // Tag command
    translations_["cmd.tag.no_tags"] = "未找到标签";
    translations_["cmd.tag.deleted"] = "标签已删除";
    translations_["cmd.tag.created"] = "标签已创建";
    translations_["cmd.tag.description"] = "描述";

    // Additional errors
    translations_["error.theme.invalid"] = "无效主题";
    translations_["error.action.unknown"] = "未知操作";
    translations_["error.service.message_required"] = "需要服务和消息";
    translations_["error.url.message_required"] = "需要 URL 和消息";
}

} // namespace claude
