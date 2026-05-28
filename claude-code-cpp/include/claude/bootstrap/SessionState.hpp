#pragma once

#include "../core/Types.hpp"
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

namespace claude {

/// Session switch callback
using SessionSwitchCallback = std::function<void(const String& newSessionId, const String& newProjectDir)>;

/// Session identity sub-state: IDs, paths, source, lifecycle, duration
class SessionState {
public:
    void reset();

    String sessionId() const;
    void setSessionId(const String& id);
    void regenerateSessionId(bool preserveParent = false);
    String parentSessionId() const;
    void setParentSessionId(const String& id);
    void switchSession(const String& newSessionId, const String& newProjectDir);
    void onSessionSwitch(SessionSwitchCallback cb);
    String sessionProjectDir() const;
    void setSessionProjectDir(const String& dir);

    String cwd() const;
    void setCwd(const String& path);
    String originalCwd() const;
    void setOriginalCwd(const String& path);
    String projectRoot() const;
    void setProjectRoot(const String& path);

    String sessionSource() const;
    void setSessionSource(const String& source);
    bool sessionPersistenceDisabled() const;
    void setSessionPersistenceDisabled(bool val);

    std::chrono::steady_clock::time_point sessionStartTime() const;
    void setSessionStartTime(std::chrono::steady_clock::time_point t);
    int sessionDurationSeconds() const;

private:
    mutable std::mutex mutex_;

    String sessionId_;
    String parentSessionId_;
    String sessionProjectDir_;
    std::vector<SessionSwitchCallback> sessionSwitchCallbacks_;

    String cwd_;
    String originalCwd_;
    String projectRoot_;

    String sessionSource_;
    bool sessionPersistenceDisabled_ = false;

    std::chrono::steady_clock::time_point sessionStartTime_ =
        std::chrono::steady_clock::now();
};

} // namespace claude
