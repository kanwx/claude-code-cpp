#pragma once

#include "../core/Types.hpp"
#include <mutex>

namespace claude {

/// Permission sub-state: mode, bypass flags, pairing strictness
class PermissionState {
public:
    void reset();

    String permissionMode() const;
    void setPermissionMode(const String& mode);
    bool bypassPermissions() const;
    void setBypassPermissions(bool val);
    bool sessionBypassPermissionsMode() const;
    void setSessionBypassPermissionsMode(bool val);
    bool strictToolResultPairing() const;
    void setStrictToolResultPairing(bool val);
    bool sessionTrustAccepted() const;
    void setSessionTrustAccepted(bool val);

private:
    mutable std::mutex mutex_;

    String permissionMode_;
    bool bypassPermissions_ = false;
    bool sessionBypassPermissionsMode_ = false;
    bool strictToolResultPairing_ = false;
    bool sessionTrustAccepted_ = false;
};

} // namespace claude
