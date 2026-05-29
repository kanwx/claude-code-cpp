#pragma once
#include <claude/core/Types.hpp>
#include <functional>
#include <optional>
#include <vector>

namespace claude {

/// Permission choice made by user
enum class PermissionChoice {
    AllowOnce,
    AllowSession,
    AlwaysAllow,
    DenyOnce,
    AlwaysDeny
};

/// Permission request details
struct PermissionRequest {
    String toolName;
    String arguments;
    String description;
    bool isReadOnly = true;
    bool isDestructive = false;
    String command;        // For Bash tool
    String filePath;       // For file tools
};

/// Permission UI interface for displaying permission prompts
class PermissionUI {
public:
    /// Set the callback for interactive permission prompts
    using PromptCallback = std::function<PermissionChoice(const PermissionRequest&)>;
    void setPromptCallback(PromptCallback cb) { promptCallback_ = std::move(cb); }

    /// Show a permission prompt and get user decision
    PermissionChoice prompt(const PermissionRequest& request);

    /// Non-interactive: auto-approve based on rules
    static PermissionChoice autoDecide(const PermissionRequest& request);

    /// Format a permission request for display
    static String formatRequest(const PermissionRequest& request);

    /// Format the choices available
    static String formatChoices(const PermissionRequest& request);

private:
    PromptCallback promptCallback_;
};

} // namespace claude
