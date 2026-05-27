#pragma once

#include "../core/Types.hpp"
#include "../context/GitContext.hpp"
#include "../context/SkillLoader.hpp"
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <filesystem>
#include <mutex>

namespace claude {

// ========== Attachment Types ==========

/// File attachment (user @-mentioned)
struct FileAttachment {
    String type = "file";
    String filename;
    String content;
    bool truncated = false;
    String displayPath;
};

/// Directory attachment
struct DirectoryAttachment {
    String type = "directory";
    String path;
    String content;
    String displayPath;
};

/// PDF reference attachment
struct PdfReferenceAttachment {
    String type = "pdf_reference";
    String filename;
    int pageCount = 0;
    size_t fileSize = 0;
    String displayPath;
};

/// Edited file attachment
struct EditedFileAttachment {
    String type = "edited_text_file";
    String filename;
    String snippet;
};

/// Selected lines in IDE
struct IdeSelectionAttachment {
    String type = "selected_lines_in_ide";
    String ideName;
    int lineStart = 0;
    int lineEnd = 0;
    String filename;
    String content;
    String displayPath;
};

/// Todo reminder
struct TodoReminderAttachment {
    String type = "todo_reminder";
    String content;
    int itemCount = 0;
};

/// Task reminder
struct TaskReminderAttachment {
    String type = "task_reminder";
    String content;
    int itemCount = 0;
};

/// Memory attachment
struct MemoryAttachment {
    String type = "memory";
    String path;
    String content;
    String displayPath;
};

/// Skill discovery attachment
struct SkillDiscoveryAttachment {
    String type = "skill_discovery";
    String content;
};

/// Plan mode attachment
struct PlanModeAttachment {
    String type = "plan_mode";
    String planPath;
    String content;
    bool needsApproval = false;
};

/// Git status attachment
struct GitStatusAttachment {
    String type = "git_status";
    String branch;
    String mainBranch;
    String status;
    String recentCommits;
    String userName;
};

/// System reminder attachment
struct SystemReminderAttachment {
    String type = "system_reminder";
    String content;
};

/// Generic attachment variant
struct Attachment {
    String type;  // Discriminator
    FileAttachment file;
    DirectoryAttachment directory;
    PdfReferenceAttachment pdf;
    EditedFileAttachment editedFile;
    IdeSelectionAttachment ideSelection;
    TodoReminderAttachment todo;
    TaskReminderAttachment task;
    MemoryAttachment memory;
    SkillDiscoveryAttachment skill;
    PlanModeAttachment plan;
    GitStatusAttachment git;
    SystemReminderAttachment reminder;
};

// ========== Context Injection Types ==========

/// Injected context for a turn
struct InjectedContext {
    // System context (cached for session)
    std::optional<GitStatusAttachment> gitStatus;
    std::optional<String> claudeMd;
    String currentDate;

    // User context (per-turn)
    std::vector<Attachment> attachments;

    // System reminders
    std::vector<SystemReminderAttachment> systemReminders;

    // Memory files
    std::vector<MemoryAttachment> relevantMemories;

    // Skills
    std::optional<SkillDiscoveryAttachment> skillDiscovery;

    // Plan
    std::optional<PlanModeAttachment> planContext;
};

// ========== Context Injector ==========

/// Context injector - handles context injection for each turn
class ContextInjector {
public:
    ContextInjector();

    /// Build injected context for a turn
    InjectedContext buildContext(const String& userQuery = "");

    /// Add file attachment
    void addFileAttachment(const String& path, const String& content, bool truncated = false);

    /// Add directory attachment
    void addDirectoryAttachment(const String& path, const String& content);

    /// Add edited file
    void addEditedFile(const String& path, const String& snippet);

    /// Add IDE selection
    void addIdeSelection(const String& ideName, const String& path,
                         int lineStart, int lineEnd, const String& content);

    /// Add system reminder
    void addSystemReminder(const String& content);

    /// Add PDF reference attachment
    void addPdfAttachment(const String& filename, int pageCount, size_t fileSize);

    /// Add todo reminder
    void addTodoReminder(const String& content, int itemCount);

    /// Add task reminder
    void addTaskReminder(const String& content, int itemCount);

    /// Add memory
    void addMemory(const String& path, const String& content);

    /// Set CLAUDE.md content
    void setClaudeMd(const String& content);

    /// Set git status
    void setGitStatus(const GitStatusAttachment& status);

    /// Load skills from a directory (delegates to SkillLoader)
    void loadSkills(const std::filesystem::path& skillsDir);

    /// Set plan mode context
    void setPlanMode(const PlanModeAttachment& plan);

    /// Clear all attachments (for new turn)
    void clearAttachments();

    /// Format injected context as message content
    String formatAsMessageContent(const InjectedContext& ctx);

    /// Format system reminders as string
    String formatSystemReminders(const InjectedContext& ctx);

    /// Check if has any attachments
    bool hasAttachments() const;

private:
    std::vector<Attachment> attachments_;
    std::optional<String> claudeMd_;
    std::optional<GitStatusAttachment> gitStatus_;
    std::vector<MemoryAttachment> memories_;
    std::vector<SystemReminderAttachment> systemReminders_;
    std::vector<Skill> skills_;
    SkillLoader skillLoader_;
    std::optional<PlanModeAttachment> planMode_;
    mutable std::recursive_mutex contextMutex_;  // guards all attachments and build state

    /// Get current date string
    String getCurrentDate();

    /// Load relevant memories based on query (keyword matching)
    std::vector<MemoryAttachment> loadRelevantMemories(const String& query);

    /// Split text into lowercase tokens for keyword matching
    static std::vector<String> tokenize(const String& text);

    /// Score a memory's relevance to a set of query tokens
    static int scoreMemoryRelevance(const MemoryAttachment& memory,
                                     const std::vector<String>& queryTokens);

    static constexpr int MAX_RELEVANT_MEMORIES = 5;
};

// ========== Context Formatter ==========

/// Format attachment for display
String formatAttachment(const Attachment& attachment);

/// Format git status
String formatGitStatus(const GitStatusAttachment& status);

/// Format memory
String formatMemory(const MemoryAttachment& memory);

} // namespace claude
