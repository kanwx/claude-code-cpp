#pragma once

#include <functional>
#include <string>

namespace claude {

using String = std::string;  // standalone inclusion

struct ToolEvent;  // forward declaration — defined in ApiTypes.hpp

// ========== Callback types ==========

/// Text callback (streaming output)
using OnToken = std::function<void(const String& token)>;

/// Tool event callback
using OnToolEvent = std::function<void(const ToolEvent& event)>;

/// Stream start callback
using OnStreamStart = std::function<void()>;

/// Thinking content callback
using OnThinking = std::function<void(const String& thinking)>;

} // namespace claude
