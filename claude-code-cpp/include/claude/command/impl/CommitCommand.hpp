#pragma once

#include "../PromptCommand.hpp"

namespace claude {

class CommitCommand : public PromptCommand {
public:
    String name() const override { return "commit"; }
    String description() const override { return "Create a git commit"; }

    /// Build AI prompt: asks the model to generate a commit message from the diff.
    String buildPrompt(const String& args, CommandContext& context) override;

    /// Fallback: local commit message generation when AI is unavailable.
    String execute(const String& args, CommandContext& context) override;
};

class ReviewCommand : public PromptCommand {
public:
    String name() const override { return "review"; }
    String description() const override { return "Review code changes"; }

    /// Build AI prompt: asks the model to review the diff/changes.
    String buildPrompt(const String& args, CommandContext& context) override;

    /// Fallback: local review output when AI is unavailable.
    String execute(const String& args, CommandContext& context) override;
};

class CompactCommand : public SlashCommand {
public:
    String name() const override { return "compact"; }
    String description() const override { return "Compact conversation context"; }
    String execute(const String& args, CommandContext& context) override;
};

class ConfigCommand : public SlashCommand {
public:
    String name() const override { return "config"; }
    String description() const override { return "Manage configuration"; }
    String execute(const String& args, CommandContext& context) override;
};

class MemoryCommand : public SlashCommand {
public:
    String name() const override { return "memory"; }
    String description() const override { return "Manage persistent memory"; }
    String execute(const String& args, CommandContext& context) override;
};

} // namespace claude
