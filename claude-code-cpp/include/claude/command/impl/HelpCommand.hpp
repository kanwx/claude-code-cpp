#pragma once

#include "../SlashCommand.hpp"
#include "ClearCommand.hpp"
#include "CostCommand.hpp"

namespace claude {

class HelpCommand : public SlashCommand {
public:
    String name() const override { return "help"; }
    String description() const override { return "Show help and available commands"; }
    std::vector<String> aliases() const override { return {"h", "?"}; }
    String execute(const String& args, CommandContext& context) override;
};

class ExitCommand : public SlashCommand {
public:
    String name() const override { return "exit"; }
    String description() const override { return "Exit the CLI"; }
    std::vector<String> aliases() const override { return {"quit", "q"}; }
    String execute(const String& args, CommandContext& context) override;
};

} // namespace claude
