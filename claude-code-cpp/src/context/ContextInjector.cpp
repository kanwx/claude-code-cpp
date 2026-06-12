#include "claude/context/ContextInjector.hpp"
#include "claude/core/ContentBlockParam.hpp"
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>

namespace claude {

ContextInjector::ContextInjector() = default;

InjectedContext ContextInjector::buildContext(const String& userQuery) {
    std::lock_guard lock(contextMutex_);
    InjectedContext ctx;

    // Date
    ctx.currentDate = getCurrentDate();

    // Git status
    if (gitStatus_.has_value()) {
        ctx.gitStatus = gitStatus_;
    }

    // CLAUDE.md
    if (claudeMd_.has_value()) {
        ctx.claudeMd = claudeMd_;
    }

    // Attachments
    ctx.attachments = attachments_;

    // System reminders
    ctx.systemReminders = systemReminders_;

    // Skills
    if (!skills_.empty()) {
        SkillDiscoveryAttachment skillAtt;
        skillAtt.type = "skill_discovery";
        std::ostringstream skillOss;
        skillOss << "Available skills:\n";
        for (const auto& skill : skills_) {
            skillOss << "  /" << skill.name << " — " << skill.description << "\n";
        }
        skillAtt.content = skillOss.str();
        ctx.skillDiscovery = skillAtt;
    }

    // Memories
    if (!userQuery.empty()) {
        ctx.relevantMemories = loadRelevantMemories(userQuery);
    } else {
        ctx.relevantMemories = memories_;
    }

    // Plan mode
    if (planMode_.has_value()) {
        ctx.planContext = planMode_;
    }

    return ctx;
}

void ContextInjector::addFileAttachment(const String& path, const String& content, bool truncated) {
    std::lock_guard lock(contextMutex_);
    Attachment att;
    att.type = "file";
    att.file.type = "file";
    att.file.filename = path;
    att.file.content = content;
    att.file.truncated = truncated;
    att.file.displayPath = path;
    attachments_.push_back(att);
}

void ContextInjector::addDirectoryAttachment(const String& path, const String& content) {
    std::lock_guard lock(contextMutex_);
    Attachment att;
    att.type = "directory";
    att.directory.type = "directory";
    att.directory.path = path;
    att.directory.content = content;
    att.directory.displayPath = path;
    attachments_.push_back(att);
}

void ContextInjector::addEditedFile(const String& path, const String& snippet) {
    std::lock_guard lock(contextMutex_);
    Attachment att;
    att.type = "edited_text_file";
    att.editedFile.type = "edited_text_file";
    att.editedFile.filename = path;
    att.editedFile.snippet = snippet;
    attachments_.push_back(att);
}

void ContextInjector::addIdeSelection(const String& ideName, const String& path,
                                       int lineStart, int lineEnd, const String& content) {
    std::lock_guard lock(contextMutex_);
    Attachment att;
    att.type = "selected_lines_in_ide";
    att.ideSelection.type = "selected_lines_in_ide";
    att.ideSelection.ideName = ideName;
    att.ideSelection.filename = path;
    att.ideSelection.lineStart = lineStart;
    att.ideSelection.lineEnd = lineEnd;
    att.ideSelection.content = content;
    att.ideSelection.displayPath = path;
    attachments_.push_back(att);
}

void ContextInjector::addSystemReminder(const String& content) {
    std::lock_guard lock(contextMutex_);
    SystemReminderAttachment reminder;
    reminder.type = "system_reminder";
    reminder.content = content;
    systemReminders_.push_back(reminder);
}

void ContextInjector::addPdfAttachment(const String& filename, int pageCount, size_t fileSize) {
    std::lock_guard lock(contextMutex_);
    Attachment att;
    att.type = "pdf_reference";
    att.pdf.type = "pdf_reference";
    att.pdf.filename = filename;
    att.pdf.pageCount = pageCount;
    att.pdf.fileSize = fileSize;
    att.pdf.displayPath = filename;
    attachments_.push_back(att);
}

void ContextInjector::addTodoReminder(const String& content, int itemCount) {
    std::lock_guard lock(contextMutex_);
    Attachment att;
    att.type = "todo_reminder";
    att.todo.type = "todo_reminder";
    att.todo.content = content;
    att.todo.itemCount = itemCount;
    attachments_.push_back(att);
}

void ContextInjector::addTaskReminder(const String& content, int itemCount) {
    std::lock_guard lock(contextMutex_);
    Attachment att;
    att.type = "task_reminder";
    att.task.type = "task_reminder";
    att.task.content = content;
    att.task.itemCount = itemCount;
    attachments_.push_back(att);
}

void ContextInjector::addMemory(const String& path, const String& content) {
    std::lock_guard lock(contextMutex_);
    MemoryAttachment mem;
    mem.type = "memory";
    mem.path = path;
    mem.content = content;
    mem.displayPath = path;
    memories_.push_back(mem);
}

void ContextInjector::setClaudeMd(const String& content) {
    std::lock_guard lock(contextMutex_);
    claudeMd_ = content;
}

void ContextInjector::setGitStatus(const GitStatusAttachment& status) {
    std::lock_guard lock(contextMutex_);
    gitStatus_ = status;
}

void ContextInjector::loadSkills(const std::filesystem::path& skillsDir) {
    std::lock_guard lock(contextMutex_);
    skills_ = skillLoader_.load(skillsDir);
    auto builtins = skillLoader_.getBuiltinSkills();
    skills_.insert(skills_.end(), builtins.begin(), builtins.end());
    spdlog::debug("ContextInjector: loaded {} skills", skills_.size());
}

void ContextInjector::setPlanMode(const PlanModeAttachment& plan) {
    std::lock_guard lock(contextMutex_);
    planMode_ = plan;
}

void ContextInjector::clearAttachments() {
    std::lock_guard lock(contextMutex_);
    attachments_.clear();
}

std::vector<ContentBlockParam> ContextInjector::buildUserContextBlocks(const String& userInput) {
    std::vector<ContentBlockParam> blocks;

    // User's actual input
    blocks.push_back(ContentBlockParam::makeText(userInput));

    // Git status as system-reminder
    if (gitStatus_.has_value()) {
        std::ostringstream gitOss;
        gitOss << "gitStatus: This is the git status at the start of the conversation. "
               << "Note that this status is a snapshot in time, and will not update "
               << "during the conversation.\n\n"
               << formatGitStatus(*gitStatus_);
        blocks.push_back(ContentBlockParam::makeText(
            "<system-reminder>\n" + gitOss.str() + "\n</system-reminder>"));
    }

    // CLAUDE.md as system-reminder
    if (claudeMd_.has_value() && !claudeMd_->empty()) {
        blocks.push_back(ContentBlockParam::makeText(
            "<system-reminder>\n" + *claudeMd_ + "\n</system-reminder>"));
    }

    // Date
    blocks.push_back(ContentBlockParam::makeText(
        "<system-reminder>\n" + getCurrentDate() + "\n</system-reminder>"));

    // Skills
    if (!skills_.empty()) {
        std::ostringstream skillOss;
        skillOss << "Available skills:\n";
        for (const auto& skill : skills_) {
            skillOss << "  /" << skill.name << " — " << skill.description << "\n";
        }
        blocks.push_back(ContentBlockParam::makeText(
            "<system-reminder>\n" + skillOss.str() + "\n</system-reminder>"));
    }

    // Memories (relevant to user query)
    auto ctx = buildContext(userInput);
    for (auto& mem : ctx.relevantMemories) {
        blocks.push_back(ContentBlockParam::makeText(
            "<system-reminder>\n" + formatMemory(mem) + "\n</system-reminder>"));
    }

    return blocks;
}

String ContextInjector::formatAsMessageContent(const InjectedContext& ctx) {
    std::ostringstream oss;

    // Current date
    oss << ctx.currentDate << "\n\n";

    // Git status
    if (ctx.gitStatus.has_value()) {
        oss << "gitStatus: This is the git status at the start of the conversation. "
            << "Note that this status is a snapshot in time, and will not update "
            << "during the conversation.\n\n";
        oss << "Current branch: " << ctx.gitStatus->branch << "\n";
        oss << "Main branch (you will usually use this for PRs): " << ctx.gitStatus->mainBranch << "\n";
        if (!ctx.gitStatus->userName.empty()) {
            oss << "Git user: " << ctx.gitStatus->userName << "\n";
        }
        oss << "Status:\n" << ctx.gitStatus->status << "\n";
        oss << "Recent commits:\n" << ctx.gitStatus->recentCommits << "\n\n";
    }

    // CLAUDE.md
    if (ctx.claudeMd.has_value() && !ctx.claudeMd->empty()) {
        oss << *ctx.claudeMd << "\n\n";
    }

    // Attachments
    for (const auto& att : ctx.attachments) {
        oss << formatAttachment(att) << "\n";
    }

    // Memories
    if (!ctx.relevantMemories.empty()) {
        oss << "# Relevant Memories\n";
        for (const auto& mem : ctx.relevantMemories) {
            oss << formatMemory(mem) << "\n";
        }
        oss << "\n";
    }

    // System reminders
    if (!ctx.systemReminders.empty()) {
        oss << formatSystemReminders(ctx);
    }

    // Skills
    if (ctx.skillDiscovery.has_value()) {
        oss << ctx.skillDiscovery->content << "\n";
    }

    // Plan mode
    if (ctx.planContext.has_value()) {
        oss << "# Current Plan\n";
        if (ctx.planContext->needsApproval) {
            oss << "(This plan needs your approval before implementation)\n";
        }
        oss << ctx.planContext->content << "\n\n";
    }

    return oss.str();
}

String ContextInjector::formatSystemReminders(const InjectedContext& ctx) {
    if (ctx.systemReminders.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "# System Reminders\n";

    for (const auto& reminder : ctx.systemReminders) {
        oss << "<system-reminder>\n";
        oss << reminder.content << "\n";
        oss << "</system-reminder>\n\n";
    }

    return oss.str();
}

bool ContextInjector::hasAttachments() const {
    std::lock_guard lock(contextMutex_);
    return !attachments_.empty();
}

String ContextInjector::getCurrentDate() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << "Today's date is "
        << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << "/"
        << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1) << "/"
        << std::setfill('0') << std::setw(2) << tm.tm_mday << ".";
    return oss.str();
}

std::vector<MemoryAttachment> ContextInjector::loadRelevantMemories(const String& query) {
    std::lock_guard lock(contextMutex_);
    if (query.empty() || memories_.empty()) {
        return memories_;
    }

    auto queryTokens = tokenize(query);
    if (queryTokens.empty()) {
        return memories_;
    }

    // Score and sort memories by relevance
    std::vector<std::pair<int, size_t>> scored;
    scored.reserve(memories_.size());
    for (size_t i = 0; i < memories_.size(); ++i) {
        int score = scoreMemoryRelevance(memories_[i], queryTokens);
        scored.emplace_back(score, i);
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Return top N relevant memories (score > 0), or fallback to all if none match
    std::vector<MemoryAttachment> result;
    for (const auto& [score, idx] : scored) {
        if (score > 0 && result.size() < MAX_RELEVANT_MEMORIES) {
            result.push_back(memories_[idx]);
        }
    }

    if (result.empty()) {
        return memories_;
    }
    return result;
}

std::vector<String> ContextInjector::tokenize(const String& text) {
    std::vector<String> tokens;
    std::string current;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!current.empty()) {
            if (current.size() >= 2) tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty() && current.size() >= 2) {
        tokens.push_back(current);
    }
    return tokens;
}

int ContextInjector::scoreMemoryRelevance(
    const MemoryAttachment& memory,
    const std::vector<String>& queryTokens)
{
    auto memTokens = tokenize(memory.content + " " + memory.path);
    int score = 0;
    for (const auto& qt : queryTokens) {
        for (const auto& mt : memTokens) {
            if (qt == mt) {
                score += 2;
            } else if (mt.size() >= 3 && qt.size() >= 3 &&
                       mt.find(qt) != String::npos) {
                score += 1;
            }
        }
    }
    return score;
}

// ========== Format Functions ==========

String formatAttachment(const Attachment& attachment) {
    std::ostringstream oss;

    if (attachment.type == "file") {
        oss << "File: " << attachment.file.displayPath << "\n";
        oss << attachment.file.content;
        if (attachment.file.truncated) {
            oss << "\n... (truncated)";
        }
    } else if (attachment.type == "directory") {
        oss << "Directory: " << attachment.directory.displayPath << "\n";
        oss << attachment.directory.content;
    } else if (attachment.type == "edited_text_file") {
        oss << "Edited file: " << attachment.editedFile.filename << "\n";
        oss << attachment.editedFile.snippet;
    } else if (attachment.type == "selected_lines_in_ide") {
        oss << "Selected in " << attachment.ideSelection.ideName << ": "
            << attachment.ideSelection.displayPath
            << ":" << attachment.ideSelection.lineStart
            << "-" << attachment.ideSelection.lineEnd << "\n";
        oss << attachment.ideSelection.content;
    } else if (attachment.type == "system_reminder") {
        oss << "<system-reminder>\n";
        oss << attachment.reminder.content << "\n";
        oss << "</system-reminder>";
    } else if (attachment.type == "pdf_reference") {
        oss << "PDF: " << attachment.pdf.displayPath
            << " (" << attachment.pdf.pageCount << " pages, "
            << attachment.pdf.fileSize << " bytes)";
    } else if (attachment.type == "todo_reminder") {
        oss << "Todo List (" << attachment.todo.itemCount << " items):\n";
        oss << attachment.todo.content;
    } else if (attachment.type == "task_reminder") {
        oss << "Task List (" << attachment.task.itemCount << " items):\n";
        oss << attachment.task.content;
    } else if (attachment.type == "memory") {
        oss << formatMemory(attachment.memory);
    } else if (attachment.type == "skill_discovery") {
        oss << attachment.skill.content;
    } else if (attachment.type == "plan_mode") {
        oss << "Plan: " << attachment.plan.planPath << "\n";
        if (attachment.plan.needsApproval) {
            oss << "(Needs approval)\n";
        }
        oss << attachment.plan.content;
    } else if (attachment.type == "git_status") {
        oss << formatGitStatus(attachment.git);
    }

    return oss.str();
}

String formatGitStatus(const GitStatusAttachment& status) {
    std::ostringstream oss;
    oss << "Current branch: " << status.branch << "\n";
    oss << "Main branch: " << status.mainBranch << "\n";
    if (!status.userName.empty()) {
        oss << "Git user: " << status.userName << "\n";
    }
    oss << "Status:\n" << status.status << "\n";
    oss << "Recent commits:\n" << status.recentCommits;
    return oss.str();
}

String formatMemory(const MemoryAttachment& memory) {
    std::ostringstream oss;
    oss << "Memory: " << memory.displayPath << "\n";
    oss << memory.content;
    return oss.str();
}

} // namespace claude
