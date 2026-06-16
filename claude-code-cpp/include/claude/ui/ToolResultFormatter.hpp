#pragma once

#include "claude/ui/ToolDisplayModel.hpp"

namespace claude {

struct ContentBlock;

/// Extract structured per-tool display data from a ContentBlock::ToolResult.
ToolDisplayModel formatToolResult(const ContentBlock& block);

} // namespace claude
