#include <claude/tool/impl/BashTool.hpp>
#include <claude/utils/Process.hpp>
#include <claude/permission/BashClassifier.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <memory>

namespace claude {

PermissionResult BashTool::checkPermission(const Json& input, ToolContext& context) {
    String command = input.value("command", "");
    if (command.empty()) {
        return PermissionResult::deny("No command specified");
    }

    bash_security::PreflightResult preflight;
    try {
        preflight = bash_security::preflightCheck(command, context.workDir);
    } catch (const std::exception& e) {
        spdlog::debug("BashTool::checkPermission: preflightCheck failed: {}", e.what());
        preflight.allowed = true;
        preflight.classification = BashClassification::RequirePermission;
    }
    lastPreflight_ = preflight;

    if (!preflight.allowed) {
        String reason = "Command blocked: ";
        if (preflight.injection && preflight.injection->detected) {
            reason += "Potential injection (" + preflight.injection->technique + ")";
        } else if (preflight.pathValidation && !preflight.pathValidation->allowed) {
            reason += "Path violation: " + (preflight.pathValidation->violations.empty() ?
                "sensitive path access" : preflight.pathValidation->violations[0]);
        } else {
            reason += "Auto-deny classification";
        }
        spdlog::warn("BashTool blocked: {}", reason);
        return PermissionResult::deny(reason);
    }

    if (preflight.classification == BashClassification::AlwaysAllow) {
        return PermissionResult::allow();
    }

    if (preflight.destructiveWarning) {
        String msg = "Destructive command: " + preflight.destructiveWarning->reason;
        return PermissionResult::ask(msg);
    }

    if (preflight.gitSafety && !preflight.gitSafety->safe) {
        String msg = "Git safety: " + preflight.gitSafety->warning;
        if (preflight.gitSafety->saferAlternative) {
            msg += " | Safer alternative: " + *preflight.gitSafety->saferAlternative;
        }
        return PermissionResult::ask(msg);
    }

    return PermissionResult::ask("Command requires user confirmation");
}

String BashTool::execute(const Json& input, ToolContext& context) {
    String command = input["command"];
    int timeout = input.value("timeout", 120);
    if (timeout < 1) timeout = 1;
    if (timeout > 600) timeout = 600;

    if (!lastPreflight_) {
        try {
            lastPreflight_ = bash_security::preflightCheck(command, context.workDir);
        } catch (const std::exception& e) {
            spdlog::debug("BashTool::execute: preflightCheck regex error: {}", e.what());
            lastPreflight_ = bash_security::PreflightResult{};
            lastPreflight_->allowed = true;
            lastPreflight_->classification = BashClassification::RequirePermission;
        }
    }

    auto sandbox = bash_security::detectSandbox();
    if (sandbox != bash_security::SandboxType::None) {
        spdlog::debug("BashTool: running in sandbox mode");
    }

    // Read per-tool cancel token set by StreamingToolExecutor
    Process::CancelToken cancelToken;
    if (auto tok = context.get<std::shared_ptr<std::atomic<bool>>>("__p4_cancel_token")) {
        cancelToken = *tok;
    }
    auto result = Process::execute(command, context.workDir, timeout, cancelToken);

    String output;

    if (lastPreflight_->semantics && lastPreflight_->semantics->riskLevel != "low") {
        output += "<security-context>\n";
        output += lastPreflight_->summary + "\n";
        output += "</security-context>\n\n";
    }

    output += result.stdout;

    if (!result.stderr.empty()) {
        if (!output.empty() && output.back() != '\n') output += '\n';
        output += "<stderr>\n" + result.stderr + "\n</stderr>";
    }

    if (result.timedOut) {
        if (!output.empty() && output.back() != '\n') output += '\n';
        output += "\n[ERROR: Command timed out after " + std::to_string(timeout) + " seconds]";
    }

    if (result.exitCode != 0) {
        if (!output.empty() && output.back() != '\n') output += '\n';
        output += "\n[Exit code: " + std::to_string(result.exitCode) + "]";
    }

    if (sandbox != bash_security::SandboxType::None) {
        const char* sandboxName =
            sandbox == bash_security::SandboxType::Docker ? "Docker" :
            sandbox == bash_security::SandboxType::Podman ? "Podman" :
            sandbox == bash_security::SandboxType::Bubblewrap ? "Bubblewrap" :
            sandbox == bash_security::SandboxType::Seatbelt ? "Seatbelt" : "Unknown";
        output += "\n[Running in sandbox: ";
        output += sandboxName;
        output += " environment]";
    }

    return output;
}

String BashTool::executeStreaming(const Json& input, ToolContext& context,
                                  ChunkCallback onChunk) {
    String command = input["command"];
    int timeout = input.value("timeout", 120);
    if (timeout < 1) timeout = 1;
    if (timeout > 600) timeout = 600;

    if (!lastPreflight_) {
        try {
            lastPreflight_ = bash_security::preflightCheck(command, context.workDir);
        } catch (const std::exception& e) {
            spdlog::debug("BashTool::executeStreaming: preflightCheck regex error: {}", e.what());
            lastPreflight_ = bash_security::PreflightResult{};
            lastPreflight_->allowed = true;
            lastPreflight_->classification = BashClassification::RequirePermission;
        }
    }

    // Deliver security context as first chunk if needed
    String securityPrefix;
    if (lastPreflight_->semantics && lastPreflight_->semantics->riskLevel != "low") {
        securityPrefix = "<security-context>\n" + lastPreflight_->summary + "\n</security-context>\n\n";
        if (onChunk) onChunk(securityPrefix);
    }

    // Read per-tool cancel token set by StreamingToolExecutor
    Process::CancelToken cancelToken;
    if (auto tok = context.get<std::shared_ptr<std::atomic<bool>>>("__p4_cancel_token")) {
        cancelToken = *tok;
    }

    // Execute with streaming stdout
    auto result = Process::executeStreaming(command, context.workDir, timeout,
        [&onChunk](const String& chunk) -> bool {
            if (onChunk) {
                return onChunk(chunk);
            }
            return true;
        }, cancelToken);

    // Build the final result string (matches execute() output format)
    String output = securityPrefix + result.stdout;

    if (!result.stderr.empty()) {
        if (!output.empty() && output.back() != '\n') output += '\n';
        String stderrBlock = "<stderr>\n" + result.stderr + "\n</stderr>";
        output += stderrBlock;
        if (onChunk) onChunk(stderrBlock);
    }

    if (result.timedOut) {
        String timeoutMsg = "\n[ERROR: Command timed out after " + std::to_string(timeout) + " seconds]";
        output += timeoutMsg;
        if (onChunk) onChunk(timeoutMsg);
    }

    if (result.exitCode != 0) {
        String exitMsg = "\n[Exit code: " + std::to_string(result.exitCode) + "]";
        output += exitMsg;
        if (onChunk) onChunk(exitMsg);
    }

    return output;
}

bool BashTool::isReadOnly() const {
    return false;
}

bool BashTool::isConcurrencySafe(const Json& input) const {
    String command = input.value("command", "");
    return bash_security::isReadOnlyCommand(command);
}

bool BashTool::isDestructive(const Json& input) const {
    String command = input.value("command", "");
    return bash_security::checkDestructive(command).has_value();
}

String BashTool::activityDescription(const Json& input) const {
    String cmd = input.value("command", "");
    String desc = input.value("description", "");

    if (!desc.empty()) return desc;
    if (cmd.length() > 60) cmd = cmd.substr(0, 57) + "...";

    auto semantics = bash_security::analyzeSemantics(cmd);
    const char* prefix = "";
    switch (semantics.semantics) {
        case bash_security::CommandSemantics::Read: prefix = "[read] "; break;
        case bash_security::CommandSemantics::Delete: prefix = "[delete] "; break;
        case bash_security::CommandSemantics::Network: prefix = "[network] "; break;
        case bash_security::CommandSemantics::Install: prefix = "[install] "; break;
        case bash_security::CommandSemantics::GitOperation: prefix = "[git] "; break;
        case bash_security::CommandSemantics::Docker: prefix = "[docker] "; break;
        default: break;
    }
    return String(prefix) + "Running: " + cmd;
}

ToolResultSummary BashTool::renderToolResult(const String& result, bool isError,
                                  bool isCancelled, bool isRejected) const {
    if (isCancelled) return ToolResultSummary::dim("Interrupted" "\xe2\x88\x99" " What should Claude do instead?");
    if (isRejected) return ToolResultSummary::dim("Tool use rejected");
    if (isError) {
        if (result.find("Exit code:") != String::npos)
            return ToolResultSummary::error(result.substr(0, 80));
        return ToolResultSummary::error("Error");
    }
    if (result.empty()) return ToolResultSummary::dim("Done");
    // Show first line of output
    auto nlPos = result.find('\n');
    String firstLine = (nlPos != String::npos) ? result.substr(0, nlPos) : result;
    return ToolResultSummary::success(firstLine);
}

} // namespace claude
