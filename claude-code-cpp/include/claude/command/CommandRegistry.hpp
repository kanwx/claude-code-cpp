#pragma once

#include "SlashCommand.hpp"
#include "PromptCommand.hpp"
#include "CommandContext.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>

namespace claude {

/// 命令注册中心
class CommandRegistry {
public:
    CommandRegistry() = default;

    // ========== 注册 ==========

    void registerCommand(std::unique_ptr<SlashCommand> cmd) {
        if (!cmd) return;

        String name = cmd->name();
        commands_[name] = std::move(cmd);

        // 注册别名
        auto& registered = commands_[name];
        for (const auto& alias : registered->aliases()) {
            aliases_[alias] = name;
        }

        spdlog::debug("Registered command: /{}", name);
    }

    // ========== 查找 ==========

    SlashCommand* findByName(const String& name) const {
        // 先检查别名
        auto aliasIt = aliases_.find(name);
        String actualName = aliasIt != aliases_.end() ? aliasIt->second : name;

        auto it = commands_.find(actualName);
        return it != commands_.end() ? it->second.get() : nullptr;
    }

    bool has(const String& name) const {
        return commands_.contains(name) || aliases_.contains(name);
    }

    // ========== 执行 ==========

    /// Execute a command by name.
    /// For Local commands, returns the string result directly.
    /// For Prompt commands, returns the prompt text prefixed with "__PROMPT__:"
    /// so the caller can detect it and inject it into the AI conversation.
    std::optional<String> execute(
        const String& name,
        const String& args,
        CommandContext& context
    ) {
        auto* cmd = findByName(name);
        if (!cmd) {
            return std::nullopt;
        }

        try {
            if (cmd->commandType() == CommandType::Prompt) {
                auto* promptCmd = static_cast<PromptCommand*>(cmd);
                String prompt = promptCmd->buildPrompt(args, context);
                return "__PROMPT__:" + prompt;
            }
            return cmd->execute(args, context);
        } catch (const std::exception& e) {
            return "Error: " + String(e.what());
        }
    }

    // ========== 遍历 ==========

    std::vector<SlashCommand*> getCommands() const {
        std::vector<SlashCommand*> result;
        result.reserve(commands_.size());
        for (const auto& [_, cmd] : commands_) {
            result.push_back(cmd.get());
        }
        return result;
    }

    std::vector<String> getCommandNames() const {
        std::vector<String> names;
        names.reserve(commands_.size());
        for (const auto& [name, _] : commands_) {
            names.push_back(name);
        }
        return names;
    }

    // ========== 帮助 ==========

    String getHelpText() const {
        String help = "Available commands:\n";
        for (const auto& [name, cmd] : commands_) {
            help += "  " + cmd->help() + "\n";
        }
        return help;
    }

    // ========== 统计 ==========

    size_t size() const { return commands_.size(); }

private:
    std::unordered_map<String, std::unique_ptr<SlashCommand>> commands_;
    std::unordered_map<String, String> aliases_;  // alias -> name
};

} // namespace claude
