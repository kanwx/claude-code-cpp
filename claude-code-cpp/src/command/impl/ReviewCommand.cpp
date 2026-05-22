#include <claude/command/impl/ReviewCommand.hpp>
#include <claude/command/CommandContext.hpp>
#include <sstream>
#include <cstdlib>
#include <array>

namespace claude {

namespace {
    String execGit(const String& cmd) {
        FILE* pipe = popen(("git " + cmd + " 2>&1").c_str(), "r");
        if (!pipe) return "";

        std::array<char, 4096> buffer;
        std::ostringstream result;
        while (fgets(buffer.data(), buffer.size(), pipe)) {
            result << buffer.data();
        }
        pclose(pipe);
        return result.str();
    }

    bool isInGitRepo() {
        return system("git rev-parse --git-dir > /dev/null 2>&1") == 0;
    }
}

String ReviewCommand::buildPrompt(const String& args, CommandContext& context) {
    if (!isInGitRepo()) {
        return "Not in a git repository.\n";
    }

    std::istringstream iss(args);
    String subcommand;
    iss >> subcommand;

    std::ostringstream oss;
    oss << "Review the following code changes and provide feedback.\n\n";

    if (subcommand == "diff" || subcommand.empty()) {
        // Gather diff for AI review
        String status = execGit("status --short");
        if (status.empty()) {
            return "No pending changes to review.\n";
        }

        oss << "Changed files:\n" << status << "\n";

        String diff = execGit("diff");
        if (diff.empty()) {
            diff = execGit("diff --cached");
        }
        if (diff.empty()) {
            return "No changes to review.";
        }

        if (diff.size() > 8000) {
            oss << diff.substr(0, 8000) << "\n... (truncated)\n";
        } else {
            oss << diff << "\n";
        }
    } else if (subcommand == "branch") {
        String branch = execGit("branch --show-current");
        if (!branch.empty() && branch.back() == '\n') branch.pop_back();

        String mainBranch = "main";
        String diffStat = execGit("diff " + mainBranch + "..." + branch + " --stat");
        if (diffStat.empty()) {
            mainBranch = "master";
        }

        oss << "Current branch: " << branch << "\n";
        oss << "Comparing with: " << mainBranch << "\n\n";

        String diff = execGit("diff " + mainBranch + "..." + branch);
        if (diff.size() > 8000) {
            oss << diff.substr(0, 8000) << "\n... (truncated)\n";
        } else {
            oss << diff << "\n";
        }
    } else if (subcommand == "commit") {
        String commitRef;
        iss >> commitRef;
        String ref = commitRef.empty() ? "HEAD" : commitRef;
        String showStat = execGit("show --stat " + ref);
        if (showStat.empty()) {
            return "Commit not found: " + ref + "\n";
        }

        oss << "Reviewing commit: " << ref << "\n\n";
        oss << showStat << "\n";

        String diff = execGit("show " + ref);
        if (diff.size() > 8000) {
            oss << diff.substr(0, 8000) << "\n... (truncated)\n";
        } else {
            oss << diff << "\n";
        }
    } else if (subcommand == "pr") {
        String prNum;
        iss >> prNum;
        if (prNum.empty()) {
            return "Usage: /review pr <number>\n";
        }
        String prInfo = execGit("pr view " + prNum);
        oss << "PR #" << prNum << ":\n\n" << prInfo << "\n";
    } else {
        return help();
    }

    oss << "\nInstructions:\n"
        << "1. Analyze the changes for bugs, security issues, and code quality problems.\n"
        << "2. Suggest specific improvements with code examples where appropriate.\n"
        << "3. Note any potential performance or maintainability concerns.\n"
        << "4. Format your review with clear sections and priorities (critical/important/minor).\n";

    return oss.str();
}

String ReviewCommand::execute(const String& args, CommandContext& context) {
    std::istringstream iss(args);
    String subcommand;
    iss >> subcommand;

    if (!isInGitRepo()) {
        return "Not in a git repository.\n";
    }

    if (subcommand == "diff" || subcommand.empty()) {
        return reviewDiff();
    }

    if (subcommand == "branch") {
        return reviewBranch();
    }

    if (subcommand == "pr") {
        String prNum;
        iss >> prNum;
        return reviewPr(prNum);
    }

    if (subcommand == "commit") {
        String commitRef;
        iss >> commitRef;
        return reviewCommit(commitRef);
    }

    return help();
}

String ReviewCommand::reviewDiff() {
    std::ostringstream oss;
    oss << "=== Pending Changes ===\n\n";

    // 获取状态
    String status = execGit("status --short");
    if (status.empty()) {
        oss << "No pending changes.\n";
        return oss.str();
    }

    oss << "Changed files:\n";
    oss << status << "\n";

    // 获取 diff 统计
    String diffStat = execGit("diff --stat");
    if (!diffStat.empty()) {
        oss << "\nDiff statistics:\n";
        oss << diffStat << "\n";
    }

    // 获取 diff
    String diff = execGit("diff");
    if (!diff.empty()) {
        oss << "\n=== Diff ===\n";
        // 限制输出长度
        if (diff.size() > 5000) {
            oss << diff.substr(0, 5000) << "\n... (truncated)\n";
        } else {
            oss << diff << "\n";
        }
    }

    return oss.str();
}

String ReviewCommand::reviewBranch() {
    std::ostringstream oss;
    oss << "=== Branch Review ===\n\n";

    // 当前分支
    String branch = execGit("branch --show-current");
    // 移除换行
    if (!branch.empty() && branch.back() == '\n') branch.pop_back();

    oss << "Current branch: " << branch << "\n\n";

    // 获取与 main 的差异
    String mainBranch = "main";
    String diffStat = execGit("diff " + mainBranch + "..." + branch + " --stat");

    if (diffStat.empty()) {
        // 尝试 master
        mainBranch = "master";
        diffStat = execGit("diff " + mainBranch + "..." + branch + " --stat");
    }

    if (!diffStat.empty()) {
        oss << "Changes vs " << mainBranch << ":\n";
        oss << diffStat << "\n";

        // 获取提交列表
        String commits = execGit("log " + mainBranch + ".." + branch + " --oneline");
        if (!commits.empty()) {
            oss << "\nCommits:\n";
            oss << commits << "\n";
        }
    } else {
        oss << "No differences from " << mainBranch << ".\n";
    }

    return oss.str();
}

String ReviewCommand::reviewPr(const String& prNum) {
    std::ostringstream oss;

    if (prNum.empty()) {
        oss << "Usage: /review pr <number>\n\n";

        // 显示当前分支的 PR 信息
        String prList = execGit("pr list --state open");
        if (!prList.empty()) {
            oss << "Open PRs:\n" << prList << "\n";
        }

        return oss.str();
    }

    oss << "=== PR #" << prNum << " Review ===\n\n";

    String prInfo = execGit("pr view " + prNum);
    oss << prInfo << "\n";

    return oss.str();
}

String ReviewCommand::reviewCommit(const String& commitRef) {
    std::ostringstream oss;

    String ref = commitRef.empty() ? "HEAD" : commitRef;

    oss << "=== Commit Review: " << ref << " ===\n\n";

    // 显示提交信息
    String showStat = execGit("show --stat " + ref);
    if (showStat.empty()) {
        return "Commit not found: " + ref + "\n";
    }

    oss << showStat << "\n";

    return oss.str();
}

String ReviewCommand::help() {
    return R"(Usage: /review <command> [args]

Commands:
  diff              Review pending changes (default)
  branch            Review current branch vs main/master
  pr <number>       Review a pull request
  commit [ref]      Review a specific commit

Examples:
  /review
  /review diff
  /review branch
  /review pr 123
  /review commit HEAD~1
)";
}

} // namespace claude
