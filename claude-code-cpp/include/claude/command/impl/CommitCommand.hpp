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

} // namespace claude
