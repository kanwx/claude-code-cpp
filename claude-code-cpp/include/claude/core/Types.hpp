#pragma once

#include <string>
#include <nlohmann/json.hpp>

// Backward-compatible sub-headers (must be included at file scope,
// not inside namespace claude, because each sub-header opens its own
// namespace block)
#include "ApiTypes.hpp"
#include "Result.hpp"
#include "Callbacks.hpp"

namespace claude {

// ========== Fundamental type aliases ==========
// These are also defined in sub-headers for standalone inclusion;
// C++ allows the same using-declaration to appear in the same namespace
// across translation units as long as it is identical.

using Json = nlohmann::json;
using String = std::string;

// ========== Enums ==========
// (Also defined in ApiTypes.hpp for standalone inclusion)

// NOTE: APIProvider, CacheScope, MessageRole, StreamChunkType, ToolEventPhase
// are all now defined in ApiTypes.hpp. They are still accessible via
// #include "Types.hpp" through the umbrella redirect above.

} // namespace claude
