#pragma once

#include "SlashCommand.hpp"

namespace claude {

/// A command that returns a prompt for the AI model to process.
/// The prompt is injected into the conversation as a user message,
/// and the AI response becomes the command result.
class PromptCommand : public SlashCommand {
public:
    CommandType commandType() const override { return CommandType::Prompt; }

    /// Build the prompt to send to the AI model.
    /// Returns the prompt text that will be injected into the conversation.
    /// By default, delegates to execute() — subclasses should override
    /// buildPrompt() for AI-delegated behavior and keep execute() as
    /// a fallback for when AI is unavailable.
    virtual String buildPrompt(const String& args, CommandContext& context) {
        return execute(args, context);
    }
};

} // namespace claude
