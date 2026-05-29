#pragma once

#include "../core/Types.hpp"

namespace claude {

/// Convert tool name + JSON input to a human-readable activity description.
/// @param toolName  The tool name (e.g. "Read", "Bash", "Grep")
/// @param jsonInput The JSON input string (e.g. R"({"file_path":"src/main.ts"})")
/// @param active    True = present tense ("Reading"), false = past tense ("Read")
String getActivityDescription(const String& toolName, const String& jsonInput, bool active = true);

} // namespace claude
