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

    for (const auto& msg : *messages) {
        Element el;
        switch (msg.type) {
            case DisplayMessage::Type::UserPrompt: {
                UserPromptComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::AssistantText: {
                AssistantTextComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::AssistantThinking: {
                AssistantThinkingComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::AssistantToolUse: {
                ToolUseComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::UserToolResult: {
                ToolResultComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::SystemInfo: {
                SystemInfoComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::SystemError: {
                SystemErrorComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::TurnDuration: {
                TurnDurationComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::CompactBoundary: {
                CompactBoundaryComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::CollapsedReadSearch: {
                CollapsedReadSearchComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::GroupedToolUse: {
                GroupedToolUseComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::AgentProgress: {
                AgentProgressComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::UserToolSuccess: {
                UserToolSuccessComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::UserToolError: {
                UserToolErrorComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::UserToolRejected: {
                UserToolRejectedComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::UserToolCanceled: {
                UserToolCanceledComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            case DisplayMessage::Type::AssistantRedactedThinking: {
                AssistantRedactedThinkingComponent c(msg, *ctx);
                el = c.OnRender();
                break;
            }
            // PermissionPrompt and HookSummary are handled by dedicated
            // overlay/modal components, not in the message stream.
            // Fall through to the default handler.
            case DisplayMessage::Type::PermissionPrompt:
            case DisplayMessage::Type::HookSummary:
            default:
                el = paragraph(msg.text) | dim;
                break;
        }
        els.push_back(el);
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
