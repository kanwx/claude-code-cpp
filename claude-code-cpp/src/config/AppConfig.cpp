#include <claude/config/AppConfig.hpp>
#include <claude/utils/I18n.hpp>
#include <claude/utils/Provider.hpp>
#include <claude/context/SystemPromptBuilder.hpp>
#include <claude/constants/Prompts.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace claude {

AppConfig::AppConfig() {
    setDefaults();
    globalConfigPath_ = findGlobalConfigPath();
}

// ========== 加载 ==========

void AppConfig::load() {
    // 1. 加载全局配置
    loadGlobal();

    // 2. 加载项目本地配置 (覆盖全局)
    localConfigPath_ = findLocalConfigPath();
    if (!localConfigPath_.empty() && std::filesystem::exists(localConfigPath_)) {
        Json localConfig;
        std::ifstream file(localConfigPath_);
        if (file) {
            try {
                localConfig = Json::parse(file);
                config_ = merge(config_, localConfig);
            } catch (...) {}
        }
    }

    // 3. 环境变量覆盖
    applyEnvOverrides();
}

void AppConfig::loadGlobal() {
    if (std::filesystem::exists(globalConfigPath_)) {
        loadFromFile(globalConfigPath_);
    }
}

void AppConfig::loadLocal(const std::filesystem::path& projectDir) {
    auto localPath = projectDir / ".claude" / "settings.json";
    if (std::filesystem::exists(localPath)) {
        Json localConfig;
        std::ifstream file(localPath);
        if (file) {
            try {
                localConfig = Json::parse(file);
                config_ = merge(config_, localConfig);
            } catch (...) {}
        }
    }
}

void AppConfig::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (file) {
        try {
            Json loaded = Json::parse(file);
            config_ = merge(config_, loaded);
        } catch (...) {}
    }
}

void AppConfig::save() {
    std::ofstream file(globalConfigPath_);
    file << config_.dump(2);
}

void AppConfig::reset() {
    setDefaults();
}

// ========== API 配置 ==========

AppConfig::ApiConfig AppConfig::getApiConfig() const {
    ApiConfig api;

    // 从嵌套结构读取
    if (config_.contains("api")) {
        auto& apiObj = config_["api"];
        api.provider = apiObj.value("provider", "anthropic");
        api.model = apiObj.value("model", "");
        api.baseUrl = apiObj.value("baseUrl", "");
        api.apiKey = apiObj.value("apiKey", "");
        api.maxTokens = apiObj.value("maxTokens", 16384);
        api.temperature = apiObj.value("temperature", 1.0);
        api.timeout = apiObj.value("timeout", 120);
        api.defaultOpusModel = apiObj.value("defaultOpusModel", "");
        api.defaultSonnetModel = apiObj.value("defaultSonnetModel", "");
        api.defaultHaikuModel = apiObj.value("defaultHaikuModel", "");
        api.reasoningModel = apiObj.value("reasoningModel", "");
    }

    // 兼容旧格式 (顶层字段)
    api.provider = config_.value("provider", api.provider);
    api.model = config_.value("model", api.model);
    api.apiKey = config_.value("apiKey", api.apiKey);
    api.baseUrl = config_.value("baseUrl", api.baseUrl);

    // 设置默认值
    if (api.baseUrl.empty()) {
        if (api.provider == "anthropic") {
            api.baseUrl = "https://api.anthropic.com/v1";
        } else if (api.provider == "openai") {
            api.baseUrl = "https://api.openai.com/v1";
        }
    }

    return api;
}

void AppConfig::setApiConfig(const ApiConfig& config) {
    config_["api"] = {
        {"provider", config.provider},
        {"model", config.model},
        {"baseUrl", config.baseUrl},
        {"apiKey", config.apiKey},
        {"maxTokens", config.maxTokens},
        {"temperature", config.temperature},
        {"timeout", config.timeout},
        {"defaultOpusModel", config.defaultOpusModel},
        {"defaultSonnetModel", config.defaultSonnetModel},
        {"defaultHaikuModel", config.defaultHaikuModel},
        {"reasoningModel", config.reasoningModel}
    };
}

String AppConfig::getProvider() const {
    return getApiConfig().provider;
}

void AppConfig::setProvider(const String& provider) {
    config_["api"]["provider"] = provider;
}

String AppConfig::getModel() const {
    return getApiConfig().model;
}

void AppConfig::setModel(const String& model) {
    config_["api"]["model"] = model;
}

String AppConfig::getApiKey() const {
    return getApiConfig().apiKey;
}

void AppConfig::setApiKey(const String& key) {
    config_["api"]["apiKey"] = key;
}

String AppConfig::getBaseUrl() const {
    return getApiConfig().baseUrl;
}

void AppConfig::setBaseUrl(const String& url) {
    config_["api"]["baseUrl"] = url;
}

int AppConfig::getMaxTokens() const {
    return getApiConfig().maxTokens;
}

void AppConfig::setMaxTokens(int tokens) {
    config_["api"]["maxTokens"] = tokens;
}

// ========== UI 配置 ==========

AppConfig::UiConfig AppConfig::getUiConfig() const {
    UiConfig ui;

    if (config_.contains("ui")) {
        auto& uiObj = config_["ui"];
        ui.theme = uiObj.value("theme", "default");
        ui.language = uiObj.value("language", "auto");
        ui.outputStyle = uiObj.value("outputStyle", "default");
        ui.colorEnabled = uiObj.value("colorEnabled", true);
        ui.vimMode = uiObj.value("vimMode", false);
        ui.showLineNumbers = uiObj.value("showLineNumbers", true);
        ui.tabWidth = uiObj.value("tabWidth", 4);
    }

    // 兼容旧格式
    ui.theme = config_.value("theme", ui.theme);
    ui.language = config_.value("language", ui.language);
    ui.vimMode = config_.value("vimMode", ui.vimMode);

    return ui;
}

void AppConfig::setUiConfig(const UiConfig& config) {
    config_["ui"] = {
        {"theme", config.theme},
        {"language", config.language},
        {"outputStyle", config.outputStyle},
        {"colorEnabled", config.colorEnabled},
        {"vimMode", config.vimMode},
        {"showLineNumbers", config.showLineNumbers},
        {"tabWidth", config.tabWidth}
    };
}

String AppConfig::getTheme() const {
    return getUiConfig().theme;
}

void AppConfig::setTheme(const String& theme) {
    config_["ui"]["theme"] = theme;
    save();
}

String AppConfig::getLanguage() const {
    return getUiConfig().language;
}

void AppConfig::setLanguage(const String& lang) {
    config_["ui"]["language"] = lang;
    save();
}

bool AppConfig::getVimMode() const {
    return getUiConfig().vimMode;
}

void AppConfig::setVimMode(bool enabled) {
    config_["ui"]["vimMode"] = enabled;
    save();
}

bool AppConfig::getColorEnabled() const {
    return getUiConfig().colorEnabled;
}

void AppConfig::setColorEnabled(bool enabled) {
    config_["ui"]["colorEnabled"] = enabled;
    save();
}

// ========== 权限配置 ==========

AppConfig::PermissionConfig AppConfig::getPermissionConfig() const {
    PermissionConfig perm;

    if (config_.contains("permissions")) {
        auto& permObj = config_["permissions"];
        perm.mode = permObj.value("mode", "default");
        perm.allow = permObj.value("allow", std::vector<String>{});
        perm.deny = permObj.value("deny", std::vector<String>{});
        perm.autoApproveThreshold = permObj.value("autoApproveThreshold", 0);
        perm.disableBypassPermissionsMode = permObj.value("disableBypassPermissionsMode", "");
    }

    // 兼容旧格式
    perm.mode = config_.value("permissionMode", perm.mode);

    return perm;
}

void AppConfig::setPermissionConfig(const PermissionConfig& config) {
    config_["permissions"] = {
        {"mode", config.mode},
        {"allow", config.allow},
        {"deny", config.deny},
        {"autoApproveThreshold", config.autoApproveThreshold}
    };
    if (!config.disableBypassPermissionsMode.empty()) {
        config_["permissions"]["disableBypassPermissionsMode"] = config.disableBypassPermissionsMode;
    }
}

String AppConfig::getPermissionMode() const {
    return getPermissionConfig().mode;
}

void AppConfig::setPermissionMode(const String& mode) {
    config_["permissions"]["mode"] = mode;
    save();
}

// ========== 性能配置 ==========

AppConfig::PerformanceConfig AppConfig::getPerformanceConfig() const {
    PerformanceConfig perf;

    if (config_.contains("performance")) {
        auto& perfObj = config_["performance"];
        perf.cachingEnabled = perfObj.value("cachingEnabled", true);
        perf.compactEnabled = perfObj.value("compactEnabled", true);
        perf.maxContextTokens = perfObj.value("maxContextTokens", 200000);
        perf.compactThreshold = perfObj.value("compactThreshold", 180000);
        perf.parallelTools = perfObj.value("parallelTools", true);
        perf.maxParallelTools = perfObj.value("maxParallelTools", 4);
    }

    // 兼容旧格式
    perf.cachingEnabled = config_.value("cachingEnabled", perf.cachingEnabled);

    return perf;
}

void AppConfig::setPerformanceConfig(const PerformanceConfig& config) {
    config_["performance"] = {
        {"cachingEnabled", config.cachingEnabled},
        {"compactEnabled", config.compactEnabled},
        {"maxContextTokens", config.maxContextTokens},
        {"compactThreshold", config.compactThreshold},
        {"parallelTools", config.parallelTools},
        {"maxParallelTools", config.maxParallelTools}
    };
}

bool AppConfig::getCachingEnabled() const {
    return getPerformanceConfig().cachingEnabled;
}

void AppConfig::setCachingEnabled(bool enabled) {
    config_["performance"]["cachingEnabled"] = enabled;
    save();
}

int AppConfig::getMaxContextTokens() const {
    return getPerformanceConfig().maxContextTokens;
}

void AppConfig::setMaxContextTokens(int tokens) {
    config_["performance"]["maxContextTokens"] = tokens;
    save();
}

// ========== 日志配置 ==========

AppConfig::LogConfig AppConfig::getLogConfig() const {
    LogConfig log;

    if (config_.contains("log")) {
        auto& logObj = config_["log"];
        log.level = logObj.value("level", "info");
        log.verbose = logObj.value("verbose", false);
        log.telemetry = logObj.value("telemetry", true);
        log.logFile = logObj.value("logFile", "");
    }

    // 兼容旧格式
    log.verbose = config_.value("verbose", log.verbose);

    return log;
}

void AppConfig::setLogConfig(const LogConfig& config) {
    config_["log"] = {
        {"level", config.level},
        {"verbose", config.verbose},
        {"telemetry", config.telemetry},
        {"logFile", config.logFile}
    };
}

bool AppConfig::getVerbose() const {
    return getLogConfig().verbose;
}

void AppConfig::setVerbose(bool verbose) {
    config_["log"]["verbose"] = verbose;
}

// ========== 认知后端配置 ==========

AppConfig::CognitiveConfig AppConfig::getCognitiveConfig() const {
    CognitiveConfig cognitive;

    if (config_.contains("cognitive")) {
        auto& cogObj = config_["cognitive"];
        cognitive.enabled = cogObj.value("enabled", false);
        cognitive.mode = cogObj.value("mode", "stdio");
        cognitive.endpoint = cogObj.value("endpoint", "http://localhost:9090/cognitive");
        cognitive.command = cogObj.value("command", "");

        if (cogObj.contains("cache")) {
            auto& cacheObj = cogObj["cache"];
            cognitive.cache.enabled = cacheObj.value("enabled", true);
            cognitive.cache.ttl = cacheObj.value("ttl", 3600);
            cognitive.cache.maxSize = cacheObj.value("maxSize", 1024ULL * 1024ULL * 1024ULL);
        }

        if (cogObj.contains("collaboration")) {
            auto& collabObj = cogObj["collaboration"];
            cognitive.collaboration.intentThreshold = collabObj.value("intentThreshold", 0.7);
            cognitive.collaboration.maxInferenceDepth = collabObj.value("maxInferenceDepth", 5);
            cognitive.collaboration.symbolicWeight = collabObj.value("symbolicWeight", 0.4);
            cognitive.collaboration.neuralWeight = collabObj.value("neuralWeight", 0.3);
            cognitive.collaboration.llmWeight = collabObj.value("llmWeight", 0.3);
        }
    }

    return cognitive;
}

void AppConfig::setCognitiveConfig(const CognitiveConfig& config) {
    config_["cognitive"] = {
        {"enabled", config.enabled},
        {"mode", config.mode},
        {"endpoint", config.endpoint},
        {"command", config.command},
        {"cache", {
            {"enabled", config.cache.enabled},
            {"ttl", config.cache.ttl},
            {"maxSize", config.cache.maxSize}
        }},
        {"collaboration", {
            {"intentThreshold", config.collaboration.intentThreshold},
            {"maxInferenceDepth", config.collaboration.maxInferenceDepth},
            {"symbolicWeight", config.collaboration.symbolicWeight},
            {"neuralWeight", config.collaboration.neuralWeight},
            {"llmWeight", config.collaboration.llmWeight}
        }}
    };
}

bool AppConfig::getCognitiveEnabled() const {
    return getCognitiveConfig().enabled;
}

void AppConfig::setCognitiveEnabled(bool enabled) {
    config_["cognitive"]["enabled"] = enabled;
    save();
}

SystemPromptBuilder AppConfig::createSystemPromptBuilder() const {
    // 构建完整的系统提示词 (匹配原版 TS)
    SystemPromptBuilder builder;

    // 设置环境信息
    EnvironmentInfo env;
    env.cwd = std::filesystem::current_path().string();
    env.platform =
#ifdef __APPLE__
        "darwin";
#elif defined(__linux__)
        "linux";
#elif defined(_WIN32)
        "win32";
#else
        "unknown";
#endif
    env.shell = std::getenv("SHELL") ? std::getenv("SHELL") : "unknown";

    // 检测 git
    try {
        std::filesystem::path gitDir = std::filesystem::current_path() / ".git";
        env.isGit = std::filesystem::exists(gitDir);
    } catch (...) {
        env.isGit = false;
    }

    env.modelId = getModel();
    env.modelName = getModel();  // TODO: Get marketing name

    builder.withEnvironment(env);
    builder.withWorkDir(env.cwd);

    // 设置 UI 配置
    auto uiConfig = getUiConfig();
    PromptSettings settings;
    settings.language = uiConfig.language;
    settings.outputStyle = uiConfig.outputStyle;
    settings.colorEnabled = uiConfig.colorEnabled;
    settings.vimMode = uiConfig.vimMode;
    builder.withSettings(settings);

    return builder;
}

String AppConfig::getSystemPrompt() const {
    // 允许自定义系统提示词
    if (config_.contains("systemPrompt")) {
        return config_["systemPrompt"].get<String>();
    }

    auto builder = createSystemPromptBuilder();
    String prompt = builder.build();

    // 日志确认新提示词被使用
    spdlog::debug("System prompt length: {} chars", prompt.length());
    spdlog::debug("Contains 'Output efficiency': {}",
        prompt.find("Output efficiency") != String::npos);

    return prompt;
}

std::vector<TextBlockParam> AppConfig::getSystemPromptBlocks() const {
    // 自定义提示词: 构建单块 (无缓存边界)
    if (config_.contains("systemPrompt")) {
        String customPrompt = config_["systemPrompt"].get<String>();
        TextBlockParam block;
        block.type = "text";
        block.text = customPrompt;
        block.cache_control = CacheControl{.type = "ephemeral", .scope = CacheScope::Org};
        return {block};
    }

    auto builder = createSystemPromptBuilder();
    auto blocks = builder.buildBlocks();

    spdlog::debug("System prompt blocks: {} (with cache_control)", blocks.size());
    return blocks;
}

// ========== 路径 ==========

std::filesystem::path AppConfig::getGlobalConfigPath() const {
    return globalConfigPath_;
}

std::filesystem::path AppConfig::getLocalConfigPath() const {
    return localConfigPath_;
}

std::filesystem::path AppConfig::getMemoryPath() const {
    return globalConfigPath_.parent_path() / "memory";
}

std::filesystem::path AppConfig::getSessionPath() const {
    return globalConfigPath_.parent_path() / "sessions";
}

std::filesystem::path AppConfig::getCachePath() const {
    return globalConfigPath_.parent_path() / "cache";
}

std::filesystem::path AppConfig::getLogPath() const {
    return globalConfigPath_.parent_path() / "logs";
}

// ========== 工具方法 ==========

Json AppConfig::merge(const Json& base, const Json& overlay) {
    Json result = base;

    if (overlay.is_object()) {
        for (auto& [key, value] : overlay.items()) {
            if (result.contains(key) && result[key].is_object() && value.is_object()) {
                result[key] = merge(result[key], value);
            } else {
                result[key] = value;
            }
        }
    }

    return result;
}

// ========== 私有方法 ==========

void AppConfig::setDefaults() {
    config_ = {
        {"api", {
            {"provider", "anthropic"},
            {"maxTokens", 16384},
            {"temperature", 1.0},
            {"timeout", 120}
        }},
        {"ui", {
            {"theme", "default"},
            {"language", "auto"},
            {"outputStyle", "default"},
            {"colorEnabled", true},
            {"vimMode", false},
            {"showLineNumbers", true},
            {"tabWidth", 4}
        }},
        {"permissions", {
            {"mode", "default"}
        }},
        {"performance", {
            {"cachingEnabled", true},
            {"compactEnabled", true},
            {"maxContextTokens", 200000},
            {"compactThreshold", 180000},
            {"parallelTools", true},
            {"maxParallelTools", 4}
        }},
        {"log", {
            {"level", "info"},
            {"verbose", false},
            {"telemetry", true}
        }}
    };
}

void AppConfig::applyEnvOverrides() {
    // Step 1: 兼容顶层 model 字段（最低优先级 — baseline）
    if (config_.contains("model") && !config_["model"].is_object()) {
        String model = config_["model"].get<String>();
        if (!model.empty()) {
            config_["api"]["model"] = model;
        }
    }

    // Step 2: 处理配置文件中的 env 字段（中优先级 — 覆盖顶层 model）
    // 顺序很重要：env.ANTHROPIC_MODEL 必须覆盖顶层 model，但不能被顶层 model 覆盖回去
    if (config_.contains("env") && config_["env"].is_object()) {
        auto& envObj = config_["env"];

        if (envObj.contains("ANTHROPIC_AUTH_TOKEN")) {
            String token = envObj["ANTHROPIC_AUTH_TOKEN"].get<String>();
            if (!token.empty()) {
                config_["api"]["apiKey"] = token;
            }
        }
        if (envObj.contains("ANTHROPIC_BASE_URL")) {
            String url = envObj["ANTHROPIC_BASE_URL"].get<String>();
            if (!url.empty()) {
                config_["api"]["baseUrl"] = url;
            }
        }
        if (envObj.contains("ANTHROPIC_MODEL")) {
            String model = envObj["ANTHROPIC_MODEL"].get<String>();
            if (!model.empty()) {
                config_["api"]["model"] = model;  // 覆盖顶层 model
            }
        }
        if (envObj.contains("ANTHROPIC_DEFAULT_OPUS_MODEL")) {
            config_["api"]["defaultOpusModel"] = envObj["ANTHROPIC_DEFAULT_OPUS_MODEL"];
        }
        if (envObj.contains("ANTHROPIC_DEFAULT_SONNET_MODEL")) {
            config_["api"]["defaultSonnetModel"] = envObj["ANTHROPIC_DEFAULT_SONNET_MODEL"];
        }
        if (envObj.contains("ANTHROPIC_DEFAULT_HAIKU_MODEL")) {
            config_["api"]["defaultHaikuModel"] = envObj["ANTHROPIC_DEFAULT_HAIKU_MODEL"];
        }
        if (envObj.contains("ANTHROPIC_REASONING_MODEL")) {
            config_["api"]["reasoningModel"] = envObj["ANTHROPIC_REASONING_MODEL"];
        }
    }

    // 兼容顶层 theme 字段
    if (config_.contains("theme") && !config_["theme"].is_object()) {
        config_["ui"]["theme"] = config_["theme"];
    }

    // 真实环境变量覆盖（最高优先级）
    const char* model = std::getenv("CLAUDE_CODE_MODEL");
    if (!model) model = std::getenv("ANTHROPIC_MODEL");
    if (model) config_["api"]["model"] = model;

    const char* apiKey = std::getenv("ANTHROPIC_API_KEY");
    if (!apiKey) apiKey = std::getenv("OPENAI_API_KEY");
    if (apiKey) config_["api"]["apiKey"] = apiKey;

    const char* baseUrl = std::getenv("ANTHROPIC_BASE_URL");
    if (baseUrl) config_["api"]["baseUrl"] = baseUrl;

    // 模型默认值环境变量
    const char* opusModel = std::getenv("ANTHROPIC_DEFAULT_OPUS_MODEL");
    if (opusModel) config_["api"]["defaultOpusModel"] = opusModel;

    const char* sonnetModel = std::getenv("ANTHROPIC_DEFAULT_SONNET_MODEL");
    if (sonnetModel) config_["api"]["defaultSonnetModel"] = sonnetModel;

    const char* haikuModel = std::getenv("ANTHROPIC_DEFAULT_HAIKU_MODEL");
    if (haikuModel) config_["api"]["defaultHaikuModel"] = haikuModel;

    const char* reasoningModel = std::getenv("ANTHROPIC_REASONING_MODEL");
    if (reasoningModel) config_["api"]["reasoningModel"] = reasoningModel;

    // UI 配置环境变量
    const char* theme = std::getenv("CLAUDE_THEME");
    if (theme) config_["ui"]["theme"] = theme;

    const char* lang = std::getenv("CLAUDE_LANGUAGE");
    if (lang) config_["ui"]["language"] = lang;

    // 权限配置环境变量
    const char* permMode = std::getenv("CLAUDE_PERMISSION_MODE");
    if (permMode) config_["permissions"]["mode"] = permMode;

    // ========== Provider 推断（原版 TS 逻辑）==========
    // 使用新的 Provider 推断系统
    APIProvider provider = getAPIProvider();
    if (!config_["api"].contains("provider") ||
        config_["api"]["provider"].get<String>().empty()) {
        config_["api"]["provider"] = providerToClientName(provider);
    }

    // 设置默认模型 (按 provider)
    if (!config_["api"].contains("model") ||
        config_["api"]["model"].get<String>().empty()) {
        config_["api"]["model"] = getDefaultSonnetModel(provider);
    }

    // 设置默认 Opus/Sonnet/Haiku 模型
    if (!config_["api"].contains("defaultOpusModel") ||
        config_["api"]["defaultOpusModel"].get<String>().empty()) {
        config_["api"]["defaultOpusModel"] = getDefaultOpusModel(provider);
    }
    if (!config_["api"].contains("defaultSonnetModel") ||
        config_["api"]["defaultSonnetModel"].get<String>().empty()) {
        config_["api"]["defaultSonnetModel"] = getDefaultSonnetModel(provider);
    }
    if (!config_["api"].contains("defaultHaikuModel") ||
        config_["api"]["defaultHaikuModel"].get<String>().empty()) {
        config_["api"]["defaultHaikuModel"] = getDefaultHaikuModel();
    }
}

std::filesystem::path AppConfig::findGlobalConfigPath() {
    const char* home = std::getenv("HOME");
    if (home) {
        std::filesystem::path configDir = std::filesystem::path(home) / ".claude";
        std::filesystem::create_directories(configDir);
        return configDir / "settings.json";
    }
    return ".claude/settings.json";
}

std::filesystem::path AppConfig::findLocalConfigPath() {
    // 当前目录及父目录查找 .claude/settings.json
    auto dir = std::filesystem::current_path();

    for (int i = 0; i < 5; ++i) {
        auto configPath = dir / ".claude" / "settings.json";
        if (std::filesystem::exists(configPath)) {
            return configPath;
        }
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }

    return {};
}

} // namespace claude
