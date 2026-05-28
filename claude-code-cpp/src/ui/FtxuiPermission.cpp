#ifdef HAS_FTXUI

#include "claude/ui/FtxuiRepl.hpp"

namespace claude {

// ========== Permission prompt (blocking for agent thread, interactive for UI) ==========

PermissionChoice FtxuiRepl::promptPermission(const String& toolName, const String& activity) {
    if (!screen_) return PermissionChoice::DenyOnce;

    // Reset state
    {
        std::lock_guard lock(permissionMutex_);
        permissionAnswered_ = false;
        permissionResult_ = PermissionChoice::DenyOnce;
    }

    // Show permission prompt on UI thread
    screen_->Post([this, tn = String(toolName), act = String(activity)]() {
        permissionPromptActive_ = true;
        permissionFocusedIndex_ = 0;
        permissionToolName_ = std::move(tn);
        permissionActivity_ = std::move(act);
    });

    // Wake up the UI loop so it renders the prompt immediately
    screen_->RequestAnimationFrame();

    // Block agent thread until user answers
    std::unique_lock lock(permissionMutex_);
    permissionCv_.wait(lock, [this]() { return permissionAnswered_; });

    return permissionResult_;
}

} // namespace claude

#endif // HAS_FTXUI
