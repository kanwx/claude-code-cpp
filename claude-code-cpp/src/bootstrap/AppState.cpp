#include <claude/bootstrap/AppState.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

namespace claude {

AppState& AppState::instance() {
    static AppState inst;
    return inst;
}

void AppState::reset() {
    // Delegate to sub-states
    metrics_.reset();
    permission_.reset();
    ui_.reset();
    session_.reset();

    // Reset remaining AppState-owned members
    std::lock_guard<std::mutex> lock(mutex_);

    mainLoopModel_.clear();
    mainLoopModelOverride_.reset();
    initialModel_.clear();
    fastModel_.clear();
    modelStrings_.reset();
    sdkBetas_.reset();

    systemPromptSectionCache_.clear();
    cachedClaudeMdContent_.clear();
    promptId_.clear();

    promptCache1hAllowlist_.reset();
    promptCache1hEligible_.reset();

    sessionIngressToken_.reset();
    oauthTokenFromFd_.reset();
    apiKeyFromFd_.reset();
    userMsgOptIn_ = false;
    clientType_.clear();

    flagSettingsPath_.reset();
    flagSettingsInline_.reset();
    allowedSettingSources_.clear();
    questionPreviewFormat_.reset();
    additionalDirectoriesForClaudeMd_.clear();

    agentColorMap_.clear();
    agentColorIndex_ = 0;
    mainThreadAgentType_.reset();
    sdkAgentProgressSummariesEnabled_ = false;

    invokedSkills_.clear();

    lastAPIRequest_.reset();
    lastAPIRequestMessages_.reset();
    lastClassifierRequests_.reset();
    lastMainRequestId_.reset();
    lastApiCompletionTimestamp_.reset();

    pendingPostCompaction_ = false;

    inMemoryErrorLog_.clear();
    slowOperations_.clear();

    initJsonSchema_.reset();
    registeredHooks_.reset();
    inlinePlugins_.clear();
    chromeFlagOverride_.reset();
    useCoworkPlugins_ = false;

    scheduledTasksEnabled_ = false;
    sessionCronTasks_.clear();

    sessionCreatedTeams_.clear();
    teleportedSessionInfo_.reset();

    allowedChannels_.clear();
    hasDevChannels_ = false;

    directConnectServerUrl_.reset();
    planSlugCache_.clear();
    lastEmittedDate_.reset();
}

// === Model Configuration ===

String AppState::mainLoopModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mainLoopModel_;
}

void AppState::setMainLoopModel(const String& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    mainLoopModel_ = model;
}

String AppState::mainLoopModelOverride() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mainLoopModelOverride_.value_or("");
}

void AppState::setMainLoopModelOverride(const String& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    mainLoopModelOverride_ = model;
}

String AppState::initialModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialModel_;
}

void AppState::setInitialModel(const String& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    initialModel_ = model;
}

String AppState::fastModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fastModel_;
}

void AppState::setFastModel(const String& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    fastModel_ = model;
}

std::optional<ModelStrings> AppState::modelStrings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return modelStrings_;
}

void AppState::setModelStrings(const ModelStrings& ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    modelStrings_ = ms;
}

void AppState::resetModelStrings() {
    std::lock_guard<std::mutex> lock(mutex_);
    modelStrings_.reset();
}

// === Cache / System Prompt ===

std::unordered_map<String, std::optional<String>> AppState::systemPromptSectionCache() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return systemPromptSectionCache_;
}

void AppState::setSystemPromptSectionCacheEntry(const String& name, const std::optional<String>& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    systemPromptSectionCache_[name] = value;
}

void AppState::clearSystemPromptSectionState() {
    std::lock_guard<std::mutex> lock(mutex_);
    systemPromptSectionCache_.clear();
}

String AppState::cachedClaudeMdContent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cachedClaudeMdContent_;
}

void AppState::setCachedClaudeMdContent(const String& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    cachedClaudeMdContent_ = content;
}

String AppState::promptId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return promptId_;
}

void AppState::setPromptId(const String& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    promptId_ = id;
}

// === Prompt Cache ===

std::optional<std::vector<String>> AppState::promptCache1hAllowlist() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return promptCache1hAllowlist_;
}

void AppState::setPromptCache1hAllowlist(const std::vector<String>& list) {
    std::lock_guard<std::mutex> lock(mutex_);
    promptCache1hAllowlist_ = list;
}

std::optional<bool> AppState::promptCache1hEligible() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return promptCache1hEligible_;
}

void AppState::setPromptCache1hEligible(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    promptCache1hEligible_ = val;
}

// === Auth ===

std::optional<String> AppState::sessionIngressToken() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionIngressToken_;
}

void AppState::setSessionIngressToken(const std::optional<String>& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionIngressToken_ = token;
}

std::optional<String> AppState::oauthTokenFromFd() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return oauthTokenFromFd_;
}

void AppState::setOauthTokenFromFd(const std::optional<String>& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    oauthTokenFromFd_ = token;
}

std::optional<String> AppState::apiKeyFromFd() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apiKeyFromFd_;
}

void AppState::setApiKeyFromFd(const std::optional<String>& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    apiKeyFromFd_ = key;
}

bool AppState::userMsgOptIn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return userMsgOptIn_;
}

void AppState::setUserMsgOptIn(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    userMsgOptIn_ = val;
}

String AppState::clientType() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clientType_;
}

void AppState::setClientType(const String& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    clientType_ = type;
}

// === Settings ===

std::optional<String> AppState::flagSettingsPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return flagSettingsPath_;
}

void AppState::setFlagSettingsPath(const std::optional<String>& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    flagSettingsPath_ = path;
}

std::optional<Json> AppState::flagSettingsInline() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return flagSettingsInline_;
}

void AppState::setFlagSettingsInline(const std::optional<Json>& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    flagSettingsInline_ = settings;
}

std::vector<int> AppState::allowedSettingSources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allowedSettingSources_;
}

void AppState::setAllowedSettingSources(const std::vector<int>& sources) {
    std::lock_guard<std::mutex> lock(mutex_);
    allowedSettingSources_ = sources;
}

bool AppState::preferThirdPartyAuthentication() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apiKeyFromFd_.has_value();
}

std::optional<String> AppState::questionPreviewFormat() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return questionPreviewFormat_;
}

void AppState::setQuestionPreviewFormat(const std::optional<String>& fmt) {
    std::lock_guard<std::mutex> lock(mutex_);
    questionPreviewFormat_ = fmt;
}

std::vector<String> AppState::additionalDirectoriesForClaudeMd() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return additionalDirectoriesForClaudeMd_;
}

void AppState::setAdditionalDirectoriesForClaudeMd(const std::vector<String>& dirs) {
    std::lock_guard<std::mutex> lock(mutex_);
    additionalDirectoriesForClaudeMd_ = dirs;
}

std::optional<std::vector<String>> AppState::sdkBetas() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sdkBetas_;
}

void AppState::setSdkBetas(const std::vector<String>& betas) {
    std::lock_guard<std::mutex> lock(mutex_);
    sdkBetas_ = betas;
}

// === Agent Coordination ===

std::unordered_map<String, String> AppState::agentColorMap() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return agentColorMap_;
}

String AppState::getNextAgentColor(const String& agentId) {
    static const String colors[] = {
        "blue", "green", "yellow", "red", "magenta", "cyan",
        "bright-blue", "bright-green", "bright-yellow", "bright-red",
        "bright-magenta", "bright-cyan"
    };
    constexpr int numColors = sizeof(colors) / sizeof(colors[0]);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agentColorMap_.find(agentId);
    if (it != agentColorMap_.end()) return it->second;

    String color = colors[agentColorIndex_ % numColors];
    agentColorIndex_++;
    agentColorMap_[agentId] = color;
    return color;
}

std::optional<String> AppState::mainThreadAgentType() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mainThreadAgentType_;
}

void AppState::setMainThreadAgentType(const std::optional<String>& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    mainThreadAgentType_ = type;
}

bool AppState::sdkAgentProgressSummariesEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sdkAgentProgressSummariesEnabled_;
}

void AppState::setSdkAgentProgressSummariesEnabled(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    sdkAgentProgressSummariesEnabled_ = val;
}

// === Invoked Skills ===

std::unordered_map<String, InvokedSkillInfo> AppState::invokedSkills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return invokedSkills_;
}

void AppState::addInvokedSkill(const String& skillName, const String& skillPath,
                               const String& content, const String& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    InvokedSkillInfo info;
    info.skillName = skillName;
    info.skillPath = skillPath;
    info.content = content;
    info.agentId = agentId;
    info.invokedAt = std::chrono::steady_clock::now();
    invokedSkills_[skillName] = std::move(info);
}

std::vector<InvokedSkillInfo> AppState::getInvokedSkillsForAgent(const String& agentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<InvokedSkillInfo> result;
    for (const auto& [name, info] : invokedSkills_) {
        if (info.agentId == agentId) {
            result.push_back(info);
        }
    }
    return result;
}

void AppState::clearInvokedSkills(const std::vector<String>& preservedAgentIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (preservedAgentIds.empty()) {
        invokedSkills_.clear();
        return;
    }
    std::unordered_set<String> preserved(preservedAgentIds.begin(), preservedAgentIds.end());
    for (auto it = invokedSkills_.begin(); it != invokedSkills_.end();) {
        if (!preserved.count(it->second.agentId)) {
            it = invokedSkills_.erase(it);
        } else {
            ++it;
        }
    }
}

void AppState::clearInvokedSkillsForAgent(const String& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = invokedSkills_.begin(); it != invokedSkills_.end();) {
        if (it->second.agentId == agentId) {
            it = invokedSkills_.erase(it);
        } else {
            ++it;
        }
    }
}

// === API Request Tracking ===

std::optional<Json> AppState::lastAPIRequest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastAPIRequest_;
}

void AppState::setLastAPIRequest(const Json& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastAPIRequest_ = req;
}

std::optional<Json> AppState::lastAPIRequestMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastAPIRequestMessages_;
}

void AppState::setLastAPIRequestMessages(const Json& msgs) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastAPIRequestMessages_ = msgs;
}

std::optional<Json> AppState::lastClassifierRequests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastClassifierRequests_;
}

void AppState::setLastClassifierRequests(const Json& reqs) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastClassifierRequests_ = reqs;
}

std::optional<String> AppState::lastMainRequestId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastMainRequestId_;
}

void AppState::setLastMainRequestId(const String& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastMainRequestId_ = id;
}

std::optional<double> AppState::lastApiCompletionTimestamp() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastApiCompletionTimestamp_;
}

void AppState::setLastApiCompletionTimestamp(double ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastApiCompletionTimestamp_ = ts;
}

// === Post-Compaction ===

bool AppState::pendingPostCompaction() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingPostCompaction_;
}

void AppState::markPostCompaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingPostCompaction_ = true;
}

bool AppState::consumePostCompaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingPostCompaction_) {
        pendingPostCompaction_ = false;
        return true;
    }
    return false;
}

// === Diagnostics ===

void AppState::addToInMemoryErrorLog(const String& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    inMemoryErrorLog_.push_back({error, std::to_string(ts)});
    if (inMemoryErrorLog_.size() > 100) {
        inMemoryErrorLog_.erase(inMemoryErrorLog_.begin());
    }
}

std::vector<ErrorLogEntry> AppState::inMemoryErrorLog() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inMemoryErrorLog_;
}

void AppState::addSlowOperation(const String& operation, double durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    slowOperations_.push_back({operation, durationMs, std::chrono::system_clock::now()});
    if (slowOperations_.size() > 50) {
        slowOperations_.erase(slowOperations_.begin());
    }
}

std::vector<SlowOperation> AppState::slowOperations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return slowOperations_;
}

// === Hooks / Plugins ===

std::optional<Json> AppState::initJsonSchema() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initJsonSchema_;
}

void AppState::setInitJsonSchema(const Json& schema) {
    std::lock_guard<std::mutex> lock(mutex_);
    initJsonSchema_ = schema;
}

std::optional<Json> AppState::registeredHooks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registeredHooks_;
}

void AppState::setRegisteredHooks(const Json& hooks) {
    std::lock_guard<std::mutex> lock(mutex_);
    registeredHooks_ = hooks;
}

void AppState::clearRegisteredHooks() {
    std::lock_guard<std::mutex> lock(mutex_);
    registeredHooks_.reset();
}

std::vector<String> AppState::inlinePlugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inlinePlugins_;
}

void AppState::setInlinePlugins(const std::vector<String>& plugins) {
    std::lock_guard<std::mutex> lock(mutex_);
    inlinePlugins_ = plugins;
}

std::optional<bool> AppState::chromeFlagOverride() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chromeFlagOverride_;
}

void AppState::setChromeFlagOverride(std::optional<bool> val) {
    std::lock_guard<std::mutex> lock(mutex_);
    chromeFlagOverride_ = val;
}

bool AppState::useCoworkPlugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return useCoworkPlugins_;
}

void AppState::setUseCoworkPlugins(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    useCoworkPlugins_ = val;
}

// === Scheduled Tasks ===

bool AppState::scheduledTasksEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scheduledTasksEnabled_;
}

void AppState::setScheduledTasksEnabled(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    scheduledTasksEnabled_ = val;
}

std::vector<SessionCronTask> AppState::sessionCronTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionCronTasks_;
}

void AppState::addSessionCronTask(const SessionCronTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionCronTasks_.push_back(task);
}

void AppState::removeSessionCronTasks(const String& taskId) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionCronTasks_.erase(
        std::remove_if(sessionCronTasks_.begin(), sessionCronTasks_.end(),
            [&](const SessionCronTask& t) { return t.id == taskId; }),
        sessionCronTasks_.end());
}

// === Team ===

std::unordered_set<String> AppState::sessionCreatedTeams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionCreatedTeams_;
}

void AppState::addSessionCreatedTeam(const String& team) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionCreatedTeams_.insert(team);
}

// === Teleport ===

std::optional<TeleportedSessionInfo> AppState::teleportedSessionInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return teleportedSessionInfo_;
}

void AppState::setTeleportedSessionInfo(const TeleportedSessionInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    teleportedSessionInfo_ = info;
}

void AppState::markFirstTeleportMessageLogged() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (teleportedSessionInfo_) {
        teleportedSessionInfo_->hasLoggedFirstMessage = true;
    }
}

// === Channels ===

std::vector<String> AppState::allowedChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allowedChannels_;
}

void AppState::setAllowedChannels(const std::vector<String>& channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    allowedChannels_ = channels;
}

bool AppState::hasDevChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasDevChannels_;
}

void AppState::setHasDevChannels(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    hasDevChannels_ = val;
}

// === Direct Connect ===

std::optional<String> AppState::directConnectServerUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return directConnectServerUrl_;
}

void AppState::setDirectConnectServerUrl(const std::optional<String>& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    directConnectServerUrl_ = url;
}

// === Plan ===

std::unordered_map<String, String> AppState::planSlugCache() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return planSlugCache_;
}

// === Last Emitted Date ===

std::optional<String> AppState::lastEmittedDate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastEmittedDate_;
}

void AppState::setLastEmittedDate(const String& date) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastEmittedDate_ = date;
}

} // namespace claude
