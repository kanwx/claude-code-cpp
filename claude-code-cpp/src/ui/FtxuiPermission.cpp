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
        permissionFeedbackResult_.clear();
    }

    // Show permission prompt on UI thread
    screen_->Post([this, tn = String(toolName), act = String(activity)]() {
        permissionPromptActive_ = true;
        permissionFocusedIndex_ = 0;
        permissionToolName_ = std::move(tn);
        permissionActivity_ = std::move(act);
        permissionDescription_.clear();
        permissionFeedbackActive_ = false;
        permissionFeedbackText_.clear();
        permissionFeedbackCursorPos_ = 0;

        // Set progress=Permission on the last matching tool_use message
        // so the UI shows "Waiting for permission…" while the prompt is active
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            if (it->type == DisplayMessage::Type::AssistantToolUse &&
                it->toolUse.toolName == permissionToolName_) {
                it->toolUse.progress = ToolProgress::Permission;
                break;
            }
        }
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
