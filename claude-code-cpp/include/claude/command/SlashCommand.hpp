#pragma once

#include "../core/Types.hpp"
#include <vector>
#include <optional>

namespace claude {

class CommandContext;

/// Command execution type
enum class CommandType {
    Local,       ///< Returns a string result directly
    Prompt,      ///< Returns a prompt to inject into the conversation for AI processing
};

/// 斜杠命令接口 —— 借鉴 Java SlashCommand 设计
class SlashCommand {
public:
    virtual ~SlashCommand() = default;

    // ========== 核心定义 ==========

    /// 命令名称 (不含 / 前缀)
    virtual String name() const = 0;

    /// 命令描述
    virtual String description() const = 0;

    /// 命令别名列表
    virtual std::vector<String> aliases() const {
        return {};
    }

    /// 命令执行类型 — Local 命令直接返回字符串，Prompt 命令返回 AI 提示
    virtual CommandType commandType() const { return CommandType::Local; }

    // ========== 执行 ==========

    /// 执行命令
    /// @param args 命令参数 (/ 后的文本去掉命令名后的部分)
    /// @param context 命令执行上下文
    /// @return 命令输出文本
    virtual String execute(const String& args, CommandContext& context) = 0;

    // ========== 帮助 ==========

    /// 获取帮助文本
    virtual String help() const {
        return "/" + name() + " - " + description();
    }
};

} // namespace claude
