#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <vector>
#include <memory>
#include <unordered_map>

#include "claude/ui/UiMessageTypes.hpp"

namespace claude {

// ========== Renderer Context ==========
// Shared state passed to all renderers during a render pass.

struct RendererContext {
    const std::vector<DisplayMessage>& messages;
    const String& streamingText;
    const String& thinkingSummary;
    bool isStreaming = false;
    bool isThinking = false;
    bool verboseTools = false;
    int tickCounter = 0;
    int terminalWidth = 80;

    // Stall detection
    std::chrono::steady_clock::time_point lastOutputTime{};
    bool streamStalled() const {
        if (lastOutputTime.time_since_epoch().count() == 0) return false;
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - lastOutputTime).count();
        return elapsed >= 3;
    }
};

// ========== MessageRenderer Base ==========

class MessageRenderer {
public:
    virtual ~MessageRenderer() = default;

    /// Render a single message into one or more FTXUI Elements.
    virtual std::vector<ftxui::Element> render(const DisplayMessage& msg,
                                                const RendererContext& ctx) = 0;

    /// Which message type(s) this renderer handles
    virtual DisplayMessage::Type targetType() const = 0;
};

// ========== RendererRegistry ==========
// Maps DisplayMessage::Type → MessageRenderer instance.
// The main OnRender loop dispatches through this.

class RendererRegistry {
public:
    /// Register a renderer for a given message type
    void registerRenderer(std::unique_ptr<MessageRenderer> renderer);

    /// Render a message by dispatching to the registered renderer.
    /// Returns empty vector if no renderer registered for this type.
    std::vector<ftxui::Element> render(const DisplayMessage& msg,
                                       const RendererContext& ctx) const;

    /// Check if a renderer is registered for a type
    bool hasRenderer(DisplayMessage::Type type) const;

private:
    std::unordered_map<int, std::unique_ptr<MessageRenderer>> renderers_;
};

} // namespace claude
