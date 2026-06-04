#ifdef HAS_FTXUI

#include <claude/ui/components/MessageList.hpp>

// All component headers — each handles one DisplayMessage::Type
#include <claude/ui/components/UserPromptComponent.hpp>
#include <claude/ui/components/AssistantTextComponent.hpp>
#include <claude/ui/components/AssistantThinkingComponent.hpp>
#include <claude/ui/components/ToolUseComponent.hpp>
#include <claude/ui/components/ToolResultComponent.hpp>
#include <claude/ui/components/SystemInfoComponent.hpp>
#include <claude/ui/components/SystemErrorComponent.hpp>
#include <claude/ui/components/TurnDurationComponent.hpp>
#include <claude/ui/components/CompactBoundaryComponent.hpp>
#include <claude/ui/components/CollapsedReadSearchComponent.hpp>
#include <claude/ui/components/GroupedToolUseComponent.hpp>
#include <claude/ui/components/AgentProgressComponent.hpp>
#include <claude/ui/components/UserToolSuccessComponent.hpp>
#include <claude/ui/components/UserToolErrorComponent.hpp>
#include <claude/ui/components/UserToolRejectedComponent.hpp>
#include <claude/ui/components/UserToolCanceledComponent.hpp>
#include <claude/ui/components/AssistantRedactedThinkingComponent.hpp>

#include <ftxui/dom/elements.hpp>

namespace claude::ui {

ftxui::Element RenderMessageList(const std::vector<DisplayMessage>* messages,
                                  const RenderContext* ctx) {
    using namespace ftxui;
    Elements els;

    auto isToolResultType = [](DisplayMessage::Type t) {
        return t == DisplayMessage::Type::UserToolResult ||
               t == DisplayMessage::Type::UserToolSuccess ||
               t == DisplayMessage::Type::UserToolError ||
               t == DisplayMessage::Type::UserToolRejected ||
               t == DisplayMessage::Type::UserToolCanceled;
    };

    auto renderSingle = [ctx](const DisplayMessage& msg) -> Element {
        switch (msg.type) {
            case DisplayMessage::Type::UserPrompt: {
                UserPromptComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::AssistantText: {
                AssistantTextComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::AssistantThinking: {
                AssistantThinkingComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::AssistantToolUse: {
                ToolUseComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::UserToolResult: {
                ToolResultComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::SystemInfo: {
                SystemInfoComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::SystemError: {
                SystemErrorComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::TurnDuration: {
                TurnDurationComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::CompactBoundary: {
                CompactBoundaryComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::CollapsedReadSearch: {
                CollapsedReadSearchComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::GroupedToolUse: {
                GroupedToolUseComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::AgentProgress: {
                AgentProgressComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::UserToolSuccess: {
                UserToolSuccessComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::UserToolError: {
                UserToolErrorComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::UserToolRejected: {
                UserToolRejectedComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::UserToolCanceled: {
                UserToolCanceledComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::AssistantRedactedThinking: {
                AssistantRedactedThinkingComponent c(msg, *ctx);
                return c.OnRender();
            }
            case DisplayMessage::Type::PermissionPrompt:
            case DisplayMessage::Type::HookSummary:
            default:
                return paragraph(msg.text) | dim;
        }
    };

    size_t i = 0;
    while (i < messages->size()) {
        const auto& msg = messages->at(i);

        // Detect tool_use immediately followed by its paired result
        if (msg.type == DisplayMessage::Type::AssistantToolUse &&
            i + 1 < messages->size() &&
            isToolResultType(messages->at(i + 1).type) &&
            messages->at(i + 1).toolResult.toolUseId == msg.toolUse.toolId) {
            // Render as nested pair: tool_use header + ⎿ indented result
            auto headerEl = renderSingle(msg);
            auto resultEl = renderSingle(messages->at(i + 1));
            els.push_back(vbox({
                std::move(headerEl),
                hbox({ text("  ⎿ ") | dim, std::move(resultEl) | flex }),
            }));
            i += 2;
            continue;
        }

        // Default: render standalone
        els.push_back(renderSingle(msg));
        i++;
    }

    return vbox(std::move(els));
}

ftxui::Component MessageListComponent(
    const std::vector<DisplayMessage>& messages,
    const RenderContext& ctx) {
    // Capture pointers — the caller must ensure messages and ctx outlive
    // this component (they are typically owned by FtxuiRepl or ContentState).
    const auto* msgs = &messages;
    const auto* c = &ctx;

    return ftxui::Renderer([msgs, c] {
        return RenderMessageList(msgs, c);
    });
}

} // namespace claude::ui

#endif // HAS_FTXUI
