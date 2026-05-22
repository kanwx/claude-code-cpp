#include <claude/command/CommandRegistry.hpp>

// Core commands (HelpCommand.hpp defines HelpCommand, ClearCommand, CostCommand)
#include <claude/command/impl/HelpCommand.hpp>
#include <claude/command/impl/VersionCommand.hpp>
#include <claude/command/impl/ModelCommand.hpp>
#include <claude/command/impl/InitCommand.hpp>
#include <claude/command/impl/DoctorCommand.hpp>
#include <claude/command/impl/StatusCommand.hpp>
#include <claude/command/impl/HistoryCommand.hpp>
#include <claude/command/impl/ResumeCommand.hpp>
#include <claude/command/impl/McpCommand.hpp>
#include <claude/command/impl/PermissionsCommand.hpp>
#include <claude/command/impl/SkillsCommand.hpp>
#include <claude/command/impl/HooksCommand.hpp>
#include <claude/command/impl/ThemeCommand.hpp>
#include <claude/command/impl/VimCommand.hpp>
#include <claude/command/impl/EffortCommand.hpp>
#include <claude/command/impl/StatsCommand.hpp>
#include <claude/command/impl/UpgradeCommand.hpp>
#include <claude/command/impl/Upgrade2Command.hpp>
#include <claude/command/impl/BugCommand.hpp>
#include <claude/command/impl/FeedbackCommand.hpp>
#include <claude/command/impl/ExportCommand.hpp>
#include <claude/command/impl/SessionCommand.hpp>
#include <claude/command/impl/OutputStyleCommand.hpp>
#include <claude/command/impl/LoginCommand.hpp>
#include <claude/command/impl/PrivacyCommand.hpp>
#include <claude/command/impl/BranchCommand.hpp>
#include <claude/command/impl/RewindCommand.hpp>
#include <claude/command/impl/TagCommand.hpp>
#include <claude/command/impl/KeybindingsCommand.hpp>

#include <claude/command/impl/CommitCommand.hpp>
#include <claude/command/impl/ReviewCommand.hpp>
#include <claude/command/impl/CompactCommand.hpp>
#include <claude/command/impl/ConfigCommand.hpp>
#include <claude/command/impl/MemoryCommand.hpp>

// P1 commands
#include <claude/command/impl/PlanCommand.hpp>
#include <claude/command/impl/LogoutCommand.hpp>
#include <claude/command/impl/EnvCommand.hpp>
#include <claude/command/impl/FastCommand.hpp>

// P2 commands
#include <claude/command/impl/DiffCommand.hpp>
#include <claude/command/impl/UsageCommand.hpp>
#include <claude/command/impl/SummaryCommand.hpp>
#include <claude/command/impl/IssueCommand.hpp>
#include <claude/command/impl/PrCommentsCommand.hpp>
#include <claude/command/impl/SecurityReviewCommand.hpp>
#include <claude/command/impl/AutofixPrCommand.hpp>
#include <claude/command/impl/ThinkbackCommand.hpp>
#include <claude/command/impl/PluginCommand.hpp>

// P3 commands
#include <claude/command/impl/IdeCommand.hpp>
#include <claude/command/impl/VoiceCommand.hpp>
#include <claude/command/impl/BridgeCommand.hpp>
#include <claude/command/impl/ProactiveCommand.hpp>
#include <claude/command/impl/ChromeCommand.hpp>
#include <claude/command/impl/MobileCommand.hpp>
#include <claude/command/impl/WorkflowsCommand.hpp>

// Additional commands
#include <claude/command/impl/AddDirCommand.hpp>
#include <claude/command/impl/AdvisorCommand.hpp>
#include <claude/command/impl/AgentsCommand.hpp>
#include <claude/command/impl/BughunterCommand.hpp>
#include <claude/command/impl/CommitPushPrCommand.hpp>
#include <claude/command/impl/ContextCommand.hpp>
#include <claude/command/impl/CopyCommand.hpp>
#include <claude/command/impl/DebugCommand.hpp>
#include <claude/command/impl/DesktopCommand.hpp>
#include <claude/command/impl/DocCommand.hpp>
#include <claude/command/impl/FilesCommand.hpp>
#include <claude/command/impl/LangCommand.hpp>
#include <claude/command/impl/ReleaseNotesCommand.hpp>
#include <claude/command/impl/RenameCommand.hpp>
#include <claude/command/impl/SwarmCommand.hpp>
#include <claude/command/impl/TasksCommand.hpp>
#include <claude/command/impl/TeleportCommand.hpp>

// Grouped command headers
#include <claude/command/impl/MoreCommands.hpp>
#include <claude/command/impl/AdditionalCommands.hpp>
#include <claude/command/impl/FinalCommands.hpp>
#include <claude/command/impl/FinalTwoCommands.hpp>

// RAG command
#include <claude/command/impl/RagCommand.hpp>

// Ontology command
#include <claude/command/impl/OntologyCommand.hpp>

namespace claude {

void registerBuiltinCommands(CommandRegistry& registry) {
    // Core commands
    registry.registerCommand(std::make_unique<HelpCommand>());
    registry.registerCommand(std::make_unique<ClearCommand>());
    registry.registerCommand(std::make_unique<ExitCommand>());
    registry.registerCommand(std::make_unique<VersionCommand>());

    // Session commands
    registry.registerCommand(std::make_unique<ResumeCommand>());
    registry.registerCommand(std::make_unique<HistoryCommand>());
    registry.registerCommand(std::make_unique<SessionCommand>());
    registry.registerCommand(std::make_unique<ExportCommand>());
    registry.registerCommand(std::make_unique<TagCommand>());
    registry.registerCommand(std::make_unique<RewindCommand>());

    // Config commands
    registry.registerCommand(std::make_unique<ConfigCommand>());
    registry.registerCommand(std::make_unique<ModelCommand>());
    registry.registerCommand(std::make_unique<ThemeCommand>());
    registry.registerCommand(std::make_unique<OutputStyleCommand>());
    registry.registerCommand(std::make_unique<EffortCommand>());
    registry.registerCommand(std::make_unique<VimCommand>());
    registry.registerCommand(std::make_unique<KeybindingsCommand>());
    registry.registerCommand(std::make_unique<LangCommand>());

    // Auth commands
    registry.registerCommand(std::make_unique<LoginCommand>());
    registry.registerCommand(std::make_unique<LogoutCommand>());
    registry.registerCommand(std::make_unique<PrivacyCommand>());

    // Git commands
    registry.registerCommand(std::make_unique<CommitCommand>());
    registry.registerCommand(std::make_unique<BranchCommand>());
    registry.registerCommand(std::make_unique<CommitPushPrCommand>());

    // Code commands
    registry.registerCommand(std::make_unique<ReviewCommand>());
    registry.registerCommand(std::make_unique<SecurityReviewCommand>());
    registry.registerCommand(std::make_unique<AutofixPrCommand>());
    registry.registerCommand(std::make_unique<DiffCommand>());
    registry.registerCommand(std::make_unique<IssueCommand>());
    registry.registerCommand(std::make_unique<PrCommentsCommand>());

    // Context commands
    registry.registerCommand(std::make_unique<CompactCommand>());
    registry.registerCommand(std::make_unique<MemoryCommand>());
    registry.registerCommand(std::make_unique<ContextCommand>());
    registry.registerCommand(std::make_unique<StatsCommand>());
    registry.registerCommand(std::make_unique<UsageCommand>());
    registry.registerCommand(std::make_unique<SummaryCommand>());
    registry.registerCommand(std::make_unique<CostCommand>());

    // Planning commands
    registry.registerCommand(std::make_unique<PlanCommand>());
    registry.registerCommand(std::make_unique<ThinkbackCommand>());
    registry.registerCommand(std::make_unique<ProactiveCommand>());

    // Tool commands
    registry.registerCommand(std::make_unique<InitCommand>());
    registry.registerCommand(std::make_unique<DoctorCommand>());
    registry.registerCommand(std::make_unique<StatusCommand>());
    registry.registerCommand(std::make_unique<McpCommand>());
    registry.registerCommand(std::make_unique<PermissionsCommand>());
    registry.registerCommand(std::make_unique<SkillsCommand>());
    registry.registerCommand(std::make_unique<HooksCommand>());
    registry.registerCommand(std::make_unique<PluginCommand>());
    registry.registerCommand(std::make_unique<RagCommand>());
    registry.registerCommand(std::make_unique<OntologyCommand>());

    // Mode commands
    registry.registerCommand(std::make_unique<FastCommand>());
    registry.registerCommand(std::make_unique<VoiceCommand>());
    registry.registerCommand(std::make_unique<BridgeCommand>());
    registry.registerCommand(std::make_unique<IdeCommand>());
    registry.registerCommand(std::make_unique<ChromeCommand>());
    registry.registerCommand(std::make_unique<MobileCommand>());
    registry.registerCommand(std::make_unique<WorkflowsCommand>());
    registry.registerCommand(std::make_unique<EnvCommand>());

    // Update commands
    registry.registerCommand(std::make_unique<UpgradeCommand>());
    registry.registerCommand(std::make_unique<Upgrade2Command>());

    // Feedback commands
    registry.registerCommand(std::make_unique<BugCommand>());
    registry.registerCommand(std::make_unique<FeedbackCommand>());

    // Additional commands
    registry.registerCommand(std::make_unique<AddDirCommand>());
    registry.registerCommand(std::make_unique<AdvisorCommand>());
    registry.registerCommand(std::make_unique<AgentsCommand>());
    registry.registerCommand(std::make_unique<BughunterCommand>());
    registry.registerCommand(std::make_unique<CopyCommand>());
    registry.registerCommand(std::make_unique<DebugCommand>());
    registry.registerCommand(std::make_unique<DesktopCommand>());
    registry.registerCommand(std::make_unique<DocCommand>());
    registry.registerCommand(std::make_unique<FilesCommand>());
    registry.registerCommand(std::make_unique<ReleaseNotesCommand>());
    registry.registerCommand(std::make_unique<RenameCommand>());
    registry.registerCommand(std::make_unique<SwarmCommand>());
    registry.registerCommand(std::make_unique<TasksCommand>());
    registry.registerCommand(std::make_unique<TeleportCommand>());

    // More commands (from grouped headers)
    registry.registerCommand(std::make_unique<ReloadPluginsCommand>());
    registry.registerCommand(std::make_unique<ResetLimitsCommand>());
    registry.registerCommand(std::make_unique<ColorCommand>());
    registry.registerCommand(std::make_unique<HeapdumpCommand>());
    registry.registerCommand(std::make_unique<UltrareviewCommand>());
    registry.registerCommand(std::make_unique<RemoteSetupCommand>());
    registry.registerCommand(std::make_unique<ThinkbackPlayCommand>());
    registry.registerCommand(std::make_unique<StatuslineCommand>());

    // Additional commands (from grouped headers)
    registry.registerCommand(std::make_unique<BtwCommand>());
    registry.registerCommand(std::make_unique<InstallGithubAppCommand>());
    registry.registerCommand(std::make_unique<InstallSlackAppCommand>());
    registry.registerCommand(std::make_unique<RateLimitCommand>());
    registry.registerCommand(std::make_unique<RemoteEnvCommand>());
    registry.registerCommand(std::make_unique<StickersCommand>());
    registry.registerCommand(std::make_unique<TerminalSetupCommand>());
    registry.registerCommand(std::make_unique<PassesCommand>());

    // Final commands
    registry.registerCommand(std::make_unique<ForkCommand>());
    registry.registerCommand(std::make_unique<BuddyCommand>());
    registry.registerCommand(std::make_unique<TorchCommand>());
    registry.registerCommand(std::make_unique<PeersCommand>());
    registry.registerCommand(std::make_unique<UltralanCommand>());
    registry.registerCommand(std::make_unique<AssistantCommand>());
    registry.registerCommand(std::make_unique<BriefCommand>());
    registry.registerCommand(std::make_unique<TmpDialogCommand>());

    // Final two commands
    registry.registerCommand(std::make_unique<OverflowTestCommand>());
    registry.registerCommand(std::make_unique<CtxInspectCommand>());

    spdlog::debug("Registered {} builtin commands", registry.size());
}

} // namespace claude
