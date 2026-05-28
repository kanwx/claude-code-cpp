#include <claude/bootstrap/SessionState.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace claude {

static String generateUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    uint32_t a = dist(gen), b = dist(gen) & 0xFFFF, c = (dist(gen) & 0x0FFF) | 0x4000;
    uint32_t d = (dist(gen) & 0x3FFF) | 0x8000, e = dist(gen), f = dist(gen) & 0xFFFF;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << a << "-"
       << std::setw(4) << (b & 0xFFFF) << "-"
       << std::setw(4) << c << "-"
       << std::setw(4) << d << "-"
       << std::setw(8) << e << std::setw(4) << f;
    return ss.str();
}

void SessionState::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionId_.clear();
    parentSessionId_.clear();
    sessionProjectDir_.clear();
    sessionSwitchCallbacks_.clear();

    cwd_.clear();
    originalCwd_.clear();
    projectRoot_.clear();

    sessionSource_.clear();
    sessionPersistenceDisabled_ = false;

    sessionStartTime_ = std::chrono::steady_clock::now();
}

// === Session Identity ===

String SessionState::sessionId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionId_;
}

void SessionState::setSessionId(const String& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionId_ = id;
}

void SessionState::regenerateSessionId(bool preserveParent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (preserveParent && !sessionId_.empty()) {
        parentSessionId_ = sessionId_;
    }
    sessionId_ = generateUUID();
}

String SessionState::parentSessionId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parentSessionId_;
}

void SessionState::setParentSessionId(const String& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    parentSessionId_ = id;
}

void SessionState::switchSession(const String& newSessionId, const String& newProjectDir) {
    std::vector<SessionSwitchCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessionId_ = newSessionId;
        sessionProjectDir_ = newProjectDir;
        callbacks = sessionSwitchCallbacks_;
    }
    for (auto& cb : callbacks) {
        cb(newSessionId, newProjectDir);
    }
}

void SessionState::onSessionSwitch(SessionSwitchCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionSwitchCallbacks_.push_back(std::move(cb));
}

String SessionState::sessionProjectDir() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionProjectDir_;
}

void SessionState::setSessionProjectDir(const String& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionProjectDir_ = dir;
}

// === Paths ===

String SessionState::cwd() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cwd_;
}

void SessionState::setCwd(const String& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    cwd_ = path;
}

String SessionState::originalCwd() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return originalCwd_;
}

void SessionState::setOriginalCwd(const String& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    originalCwd_ = path;
}

String SessionState::projectRoot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projectRoot_;
}

void SessionState::setProjectRoot(const String& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    projectRoot_ = path;
}

// === Session Lifecycle ===

String SessionState::sessionSource() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionSource_;
}

void SessionState::setSessionSource(const String& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionSource_ = source;
}

bool SessionState::sessionPersistenceDisabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionPersistenceDisabled_;
}

void SessionState::setSessionPersistenceDisabled(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionPersistenceDisabled_ = val;
}

// === Session Duration ===

std::chrono::steady_clock::time_point SessionState::sessionStartTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionStartTime_;
}

void SessionState::setSessionStartTime(std::chrono::steady_clock::time_point t) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionStartTime_ = t;
}

int SessionState::sessionDurationSeconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - sessionStartTime_).count());
}

} // namespace claude
