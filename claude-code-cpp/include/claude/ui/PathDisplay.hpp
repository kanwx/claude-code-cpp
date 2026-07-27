#pragma once

#include "claude/core/Types.hpp"
#include <string>
#include <vector>

namespace claude {

/// Truncate a file path for compact display.
///
/// Rules:
/// - Short paths (<= maxWidth) pass through unchanged
/// - Filename is always preserved
/// - Last 1-2 directory levels kept
/// - First component (root for relative, "/" + first dir for absolute) kept
/// - Middle replaced with "…"
/// - Never panics on odd inputs
String truncatePathForDisplay(const String& path, size_t maxWidth = 42);

} // namespace claude
