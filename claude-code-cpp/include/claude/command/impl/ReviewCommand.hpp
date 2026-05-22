#pragma once

#include "../PromptCommand.hpp"

namespace claude {

class ReviewCommand : public PromptCommand {
public:
    String name() const override { return "review"; }
    String description() const override { return "Review recent changes or PRs"; }

    /// Build AI prompt: asks the model to review code changes.
    String buildPrompt(const String& args, CommandContext& context) override;

    /// Fallback: local review output when AI is unavailable.
    String execute(const String& args, CommandContext& context) override;

private:
    String reviewDiff();
    String reviewBranch();
    String reviewPr(const String& prNum);
    String reviewCommit(const String& commitRef);
    String help();
};

} // namespace claude
