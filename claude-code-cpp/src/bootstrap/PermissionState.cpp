#include <claude/bootstrap/PermissionState.hpp>

namespace claude {

void PermissionState::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    permissionMode_.clear();
    bypassPermissions_ = false;
    sessionBypassPermissionsMode_ = false;
    strictToolResultPairing_ = false;
    sessionTrustAccepted_ = false;
}

String PermissionState::permissionMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return permissionMode_;
}

void PermissionState::setPermissionMode(const String& mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    permissionMode_ = mode;
}

bool PermissionState::bypassPermissions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bypassPermissions_;
}

void PermissionState::setBypassPermissions(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    bypassPermissions_ = val;
}

bool PermissionState::sessionBypassPermissionsMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionBypassPermissionsMode_;
}

void PermissionState::setSessionBypassPermissionsMode(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionBypassPermissionsMode_ = val;
}

bool PermissionState::strictToolResultPairing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return strictToolResultPairing_;
}

void PermissionState::setStrictToolResultPairing(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    strictToolResultPairing_ = val;
}

bool PermissionState::sessionTrustAccepted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionTrustAccepted_;
}

void PermissionState::setSessionTrustAccepted(bool val) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionTrustAccepted_ = val;
}

} // namespace claude
