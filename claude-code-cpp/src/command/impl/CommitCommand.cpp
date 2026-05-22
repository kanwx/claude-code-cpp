#include <claude/command/impl/CommitCommand.hpp>
#include <claude/command/CommandContext.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/utils/Process.hpp>
#include <claude/utils/I18n.hpp>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace {
/// Sentinel prefix used by CommandRegistry to signal a PromptCommand result.
/// Checked in ReplSession to inject the prompt into the AI conversation.
constexpr const char* PROMPT_PREFIX = "__PROMPT__:";
}

namespace claude {

namespace {
    // 简单的提交信息生成规则
    String generateBasicCommitMsg(const String& status, const String& diff) {
        std::istringstream statusStream(status);
        String line;
        int added = 0, modified = 0, deleted = 0, renamed = 0;

        while (std::getline(statusStream, line)) {
            if (line.empty()) continue;
            char code = line[0];
            if (code == 'A' || code == '?') added++;
            else if (code == 'M') modified++;
            else if (code == 'D') deleted++;
            else if (code == 'R') renamed++;
        }

        // 分析 diff 找关键词
        std::vector<String> keywords;
        if (diff.find("fix") != String::npos || diff.find("Fix") != String::npos ||
            diff.find("bug") != String::npos || diff.find("Bug") != String::npos) {
            keywords.push_back("fix");
        }
        if (diff.find("add") != String::npos || diff.find("Add") != String::npos ||
            diff.find("新增") != String::npos) {
            keywords.push_back("add");
        }
        if (diff.find("refactor") != String::npos || diff.find("Refactor") != String::npos) {
            keywords.push_back("refactor");
        }
        if (diff.find("update") != String::npos || diff.find("Update") != String::npos ||
            diff.find("更新") != String::npos) {
            keywords.push_back("update");
        }
        if (diff.find("remove") != String::npos || diff.find("Remove") != String::npos ||
            diff.find("删除") != String::npos) {
            keywords.push_back("remove");
        }
        if (diff.find("test") != String::npos || diff.find("Test") != String::npos ||
            diff.find("tests/") != String::npos) {
            keywords.push_back("test");
        }
        if (diff.find("doc") != String::npos || diff.find("Doc") != String::npos ||
            diff.find("README") != String::npos) {
            keywords.push_back("docs");
        }

        std::ostringstream oss;

        // 生成前缀
        if (!keywords.empty()) {
            oss << keywords[0] << ": ";
        }

        // 生成描述
        if (added > 0 && modified == 0 && deleted == 0) {
            oss << "add " << added << " file" << (added > 1 ? "s" : "");
        } else if (deleted > 0 && added == 0 && modified == 0) {
            oss << "remove " << deleted << " file" << (deleted > 1 ? "s" : "");
        } else if (modified > 0 && added == 0 && deleted == 0) {
            oss << "update " << modified << " file" << (modified > 1 ? "s" : "");
        } else {
            oss << "update code";
            if (added > 0) oss << " (+" << added << ")";
            if (modified > 0) oss << " (~" << modified << ")";
            if (deleted > 0) oss << " (-" << deleted << ")";
        }

        return oss.str();
    }

    // 从 diff 提取文件名
    std::vector<String> extractChangedFiles(const String& diff) {
        std::vector<String> files;
        std::istringstream stream(diff);
        String line;

        while (std::getline(stream, line)) {
            if (line.starts_with("+++ b/")) {
                String file = line.substr(6);
                if (std::find(files.begin(), files.end(), file) == files.end()) {
                    files.push_back(file);
                }
            }
        }

        return files;
    }
}

String CommitCommand::buildPrompt(const String& args, CommandContext& context) {
    // Gather git context for AI-driven commit message generation
    auto statusResult = Process::execute("git status --short", context.workDir);
    if (statusResult.exitCode != 0) {
        return execute(args, context);
    }
    if (statusResult.stdout.empty()) {
        return "No changes to commit.";
    }

    auto diffResult = Process::execute("git diff", context.workDir);
    auto stagedDiffResult = Process::execute("git diff --cached", context.workDir);
    String fullDiff = diffResult.stdout + "\n" + stagedDiffResult.stdout;

    if (stagedDiffResult.stdout.empty() && diffResult.stdout.empty()) {
        return "No changes to commit. Use 'git add' to stage changes.";
    }

    // Build the prompt for the AI
    std::ostringstream oss;
    oss << "Generate a git commit message for the following changes.\n\n";

    if (!args.empty()) {
        oss << "User guidance: " << args << "\n\n";
    }

    oss << "Changed files:\n" << statusResult.stdout << "\n";

    oss << "Diff:\n";
    if (fullDiff.size() > 8000) {
        oss << fullDiff.substr(0, 8000) << "\n... (truncated)\n";
    } else {
        oss << fullDiff << "\n";
    }

    oss << "\nInstructions:\n"
        << "1. Write a concise commit message in conventional commit format (type: description).\n"
        << "2. The first line should be under 72 characters.\n"
        << "3. If the changes warrant it, add a blank line then a bullet-point body.\n"
        << "4. Output ONLY the commit message — nothing else.\n";

    return oss.str();
}

String CommitCommand::execute(const String& args, CommandContext& context) {
    // 获取 git 状态
    auto statusResult = Process::execute("git status --short", context.workDir);
    if (statusResult.exitCode != 0) {
        return tr("error.git.not_repo");
    }

    if (statusResult.stdout.empty()) {
        return "No changes to commit.";
    }

    // 获取 diff
    auto diffResult = Process::execute("git diff", context.workDir);
    auto stagedDiffResult = Process::execute("git diff --cached", context.workDir);

    String fullDiff = diffResult.stdout + "\n" + stagedDiffResult.stdout;

    // 如果有暂存区的内容，优先使用
    if (stagedDiffResult.stdout.empty() && diffResult.stdout.empty()) {
        return "No changes to commit. Use 'git add' to stage changes.";
    }

    String commitMsg;

    if (!args.empty()) {
        // 用户提供了提交信息
        commitMsg = args;
    } else {
        // 自动生成提交信息
        commitMsg = generateBasicCommitMsg(statusResult.stdout, fullDiff);
    }

    // 检查是否需要先 add
    bool needsAdd = stagedDiffResult.stdout.empty();

    String cmd;
    if (needsAdd) {
        cmd = "git add -A && git commit -m \"" + commitMsg + "\"";
    } else {
        cmd = "git commit -m \"" + commitMsg + "\"";
    }

    auto commitResult = Process::execute(cmd, context.workDir);

    if (commitResult.exitCode != 0) {
        if (commitResult.stderr.find("nothing to commit") != String::npos) {
            return "Nothing to commit. Changes are already committed.";
        }
        return tr("error.git.commit_failed") + ": " + commitResult.stderr;
    }

    std::ostringstream oss;
    oss << "=== Commit Created ===\n\n";
    oss << "Message: " << commitMsg << "\n\n";

    auto changedFiles = extractChangedFiles(fullDiff);
    if (!changedFiles.empty()) {
        oss << "Files changed (" << changedFiles.size() << "):\n";
        for (size_t i = 0; i < std::min(changedFiles.size(), size_t(10)); ++i) {
            oss << "  - " << changedFiles[i] << "\n";
        }
        if (changedFiles.size() > 10) {
            oss << "  ... and " << (changedFiles.size() - 10) << " more\n";
        }
    }

    return oss.str();
}


} // namespace claude
