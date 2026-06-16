#include "claude/stream/MessagePipeline.hpp"
#include <algorithm>
#include <sstream>
#include <set>

namespace claude {

// ========== Main process entry point ==========

std::vector<ContentBlock> MessagePipeline::process(std::vector<ContentBlock> blocks) {
    if (blocks.empty()) return blocks;

    if (config_.reorderToolTrails) {
        blocks = reorderToolTrails(std::move(blocks));
    }
    if (config_.groupToolResultPairs) {
        blocks = groupToolResultPairs(std::move(blocks));
    }
    if (config_.groupConsecutiveToolUses) {
        blocks = groupConsecutiveToolUses(std::move(blocks));
    }
    if (config_.collapseReadSearch) {
        blocks = collapseReadSearchGroups(std::move(blocks));
    }
    if (config_.collapseBackgroundBash) {
        blocks = collapseBackgroundBash(std::move(blocks));
    }
    if (config_.collapseHookSummaries) {
        blocks = collapseHookSummaries(std::move(blocks));
    }
    if (config_.collapseTeammateShutdowns) {
        blocks = collapseTeammateShutdowns(std::move(blocks));
    }
    if (config_.buildLookups) {
        buildLookups(blocks);
    }

    // Post-processing: assign stable IDs to any blocks created by pipeline.
    // Scan existing IDs first to avoid collisions with IDs assigned by FtxuiRepl.
    uint64_t nextId = 1;
    for (const auto& block : blocks) {
        if (block.stableId >= nextId) {
            nextId = block.stableId + 1;
        }
    }
    for (auto& block : blocks) {
        if (block.stableId == 0) {
            block.stableId = nextId++;
        }
    }

    return blocks;
}

// ========== Pass 1: reorderToolTrails ==========

std::vector<ContentBlock> MessagePipeline::reorderToolTrails(std::vector<ContentBlock> blocks) {
    // The current stream already emits ToolProgress→ToolResult in order.
    // This pass ensures each ToolProgress is immediately followed by its
    // matching ToolResult (no intervening text), and removes orphaned
    // ToolProgress blocks whose ToolResult never arrived.
    //
    // Strategy: scan and collect ToolProgress blocks keyed by toolCallId.
    // When a matching ToolResult is found, emit the ToolProgress first,
    // then the ToolResult. Unmatched ToolProgress blocks are dropped.

    std::map<String, ContentBlock> pendingProgress;
    std::vector<ContentBlock> result;

    for (auto& block : blocks) {
        if (block.type == ContentBlock::ToolProgress) {
            // Hold the progress; emit only when its result arrives
            pendingProgress[block.toolCallId] = std::move(block);
        } else if (block.type == ContentBlock::ToolResult) {
            // Emit the paired progress first (if any), then the result
            auto it = pendingProgress.find(block.toolCallId);
            if (it != pendingProgress.end()) {
                result.push_back(std::move(it->second));
                pendingProgress.erase(it);
            }
            result.push_back(std::move(block));
        } else {
            result.push_back(std::move(block));
        }
    }

    // Drop orphaned ToolProgress blocks (no matching result)
    return result;
}

// ========== Pass 2: groupToolResultPairs ==========

std::vector<ContentBlock> MessagePipeline::groupToolResultPairs(
    std::vector<ContentBlock> blocks) {
    // Pair adjacent ToolProgress→ToolResult into ToolGroup units.
    // After reorderToolTrails, each ToolProgress is immediately followed
    // by its ToolResult. This pass wraps them into a single ToolGroup.
    //
    // Stub for now — full implementation in Task 2.
    return blocks;
}

// ========== Pass 3: groupConsecutiveToolUses ==========

std::vector<ContentBlock> MessagePipeline::groupConsecutiveToolUses(std::vector<ContentBlock> blocks) {
    // Skip grouping in verbose mode
    if (config_.verbose) return blocks;

    std::vector<ContentBlock> result;
    size_t i = 0;

    while (i < blocks.size()) {
        // Only group consecutive ToolResult blocks
        if (blocks[i].type != ContentBlock::ToolResult) {
            result.push_back(std::move(blocks[i]));
            i++;
            continue;
        }

        // Collect consecutive ToolResult blocks of the same toolName
        size_t groupStart = i;
        String groupToolName = blocks[i].toolName;
        size_t groupEnd = i + 1;

        while (groupEnd < blocks.size() &&
               blocks[groupEnd].type == ContentBlock::ToolResult &&
               blocks[groupEnd].toolName == groupToolName) {
            groupEnd++;
        }

        size_t groupSize = groupEnd - groupStart;

        if (groupSize < 2) {
            // Single tool — pass through
            result.push_back(std::move(blocks[i]));
        } else {
            // Group 2+ same-type tools
            ContentBlock group;
            group.type = ContentBlock::ToolGroup;
            group.toolName = groupToolName;
            group.toolUseIds.clear();

            // Collect children
            for (size_t j = groupStart; j < groupEnd; j++) {
                group.toolUseIds.push_back(blocks[j].toolCallId);
                group.children.push_back(std::move(blocks[j]));
            }

            // Build summary
            group.summary = ToolResultSummary::success(
                std::to_string(groupSize) + " " + groupToolName + " results",
                false, "", "[Ctrl+O to expand]");

            group.hasContentAfter = hasContentAfterIndex(blocks, groupEnd);
            result.push_back(std::move(group));
        }

        i = groupEnd;
    }

    return result;
}

// ========== Pass 4: collapseReadSearchGroups ==========

bool MessagePipeline::isCollapsibleBlock(const ContentBlock& block) const {
    if (block.type != ContentBlock::ToolResult) return false;
    if (block.summary.isError) return false;
    if (block.resultStatus == ToolResultStatus::Rejected ||
        block.resultStatus == ToolResultStatus::Cancelled) return false;

    // Use the tool classifier if registered
    if (toolClassifier_) {
        return toolClassifier_(block.toolName, 0);  // category 0 = isCollapsible
    }

    // Built-in collapsible tools
    static const std::set<String> collapsibleTools = {
        "Read", "Grep", "Glob", "LS", "Bash",
        "WebSearch", "WebFetch",
        "TaskOutput", "SendMessage", "AskUserQuestion"
    };
    return collapsibleTools.count(block.toolName) > 0;
}

GroupAccumulator::Category MessagePipeline::categorizeBlock(const ContentBlock& block) const {
    if (toolClassifier_) {
        if (toolClassifier_(block.toolName, 1)) return GroupAccumulator::Search;
        if (toolClassifier_(block.toolName, 2)) return GroupAccumulator::FileRead;
        if (toolClassifier_(block.toolName, 3)) return GroupAccumulator::FileList;
        if (toolClassifier_(block.toolName, 4)) return GroupAccumulator::MemorySearch;
    }

    // Built-in categorization
    if (block.toolName == "Grep") return GroupAccumulator::Search;
    if (block.toolName == "Glob") return GroupAccumulator::Search;
    if (block.toolName == "Read") return GroupAccumulator::FileRead;
    if (block.toolName == "LS") return GroupAccumulator::FileList;
    if (block.toolName == "Bash") return GroupAccumulator::Bash;
    if (block.toolName == "WebSearch") return GroupAccumulator::WebSearch;
    if (block.toolName == "WebFetch") return GroupAccumulator::WebFetch;

    // Default: treat as search/read
    return GroupAccumulator::Search;
}

bool MessagePipeline::isGroupBreaker(const ContentBlock& block) const {
    // Assistant text with non-empty content breaks the group
    if (block.type == ContentBlock::AnswerText && !block.text.empty()) {
        // Check if text is whitespace-only
        bool hasContent = false;
        for (char c : block.text) {
            if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
                hasContent = true;
                break;
            }
        }
        if (hasContent) return true;
    }

    // Non-collapsible tool results break the group
    if (block.type == ContentBlock::ToolResult && !isCollapsibleBlock(block)) {
        return true;
    }

    // Non-collapsible tool progress breaks the group
    if (block.type == ContentBlock::ToolProgress) {
        if (toolClassifier_) {
            if (!toolClassifier_(block.toolName, 0)) return true;
        }
        // By default, treat unknown tools as non-collapsible
        return !isCollapsibleBlock(block);
    }

    // Error messages break
    if (block.type == ContentBlock::ErrorMessage) return true;

    // System messages break (except informational)
    if (block.type == ContentBlock::SystemMessage) return true;

    return false;
}

bool MessagePipeline::hasContentAfterIndex(const std::vector<ContentBlock>& blocks, size_t index) {
    for (size_t i = index; i < blocks.size(); i++) {
        auto& b = blocks[i];
        // Thinking blocks don't count as "real" content
        if (b.type == ContentBlock::ThinkingBlock) continue;
        // Collapsible tool results don't count
        if (b.type == ContentBlock::ToolResult) {
            // If it's a collapsible read/search tool, skip
            if (b.toolName == "Read" || b.toolName == "Grep" ||
                b.toolName == "Glob" || b.toolName == "LS" ||
                b.toolName == "WebSearch" || b.toolName == "WebFetch") continue;
        }
        // Error messages don't count
        if (b.type == ContentBlock::ErrorMessage) continue;
        // System messages don't count
        if (b.type == ContentBlock::SystemMessage) continue;

        // Any other non-empty content counts
        if (!b.text.empty()) return true;
        if (b.type == ContentBlock::AnswerText) return true;
        if (b.type == ContentBlock::UserMessage) return true;
    }
    return false;
}

std::vector<ContentBlock> MessagePipeline::collapseReadSearchGroups(
    std::vector<ContentBlock> blocks) {
    if (config_.verbose) return blocks;

    std::vector<ContentBlock> result;
    GroupAccumulator acc;
    size_t i = 0;

    auto flushGroup = [&]() {
        if (acc.isEmpty()) return;

        ContentBlock group;
        group.type = ContentBlock::CollapsedGroup;
        group.summary = ToolResultSummary::success(
            buildGroupSummary(acc, acc.blockIndices.empty() ? false :
                hasContentAfterIndex(blocks, acc.blockIndices.back() + 1)),
            false, "", "[Ctrl+O to expand]");

        // Move children into the group
        for (auto idx : acc.blockIndices) {
            if (idx < blocks.size()) {
                group.children.push_back(std::move(blocks[idx]));
            }
        }

        // Copy tool_use_ids
        for (auto& id : acc.toolUseIds) {
            group.toolUseIds.push_back(id);
        }

        group.hasContentAfter = hasContentAfterIndex(blocks,
            acc.blockIndices.empty() ? result.size() : acc.blockIndices.back() + 1);

        result.push_back(std::move(group));
        acc.reset();
    };

    while (i < blocks.size()) {
        auto& block = blocks[i];

        // Check if this block breaks the current group
        if (isGroupBreaker(block)) {
            flushGroup();
            result.push_back(std::move(block));
            i++;
            continue;
        }

        // Check if collapsible
        if (isCollapsibleBlock(block)) {
            acc.blockIndices.push_back(i);
            acc.toolUseIds.insert(block.toolCallId);

            auto cat = categorizeBlock(block);
            switch (cat) {
                case GroupAccumulator::Search:
                    acc.searchCount++;
                    break;
                case GroupAccumulator::FileRead:
                    acc.readOperationCount++;
                    // Extract file path from result summary if possible
                    if (!block.summary.primaryText.empty()) {
                        // Parse "Read N lines from path" to extract path
                        auto fromPos = block.summary.primaryText.find(" from ");
                        if (fromPos != String::npos) {
                            acc.readFilePaths.insert(
                                block.summary.primaryText.substr(fromPos + 6));
                        }
                    }
                    break;
                case GroupAccumulator::FileList:
                    acc.listCount++;
                    break;
                case GroupAccumulator::Bash:
                    acc.bashCount++;
                    acc.bashCommands[block.toolCallId] = block.summary.primaryText;
                    break;
                case GroupAccumulator::WebSearch:
                    acc.webSearchCount++;
                    break;
                case GroupAccumulator::WebFetch:
                    acc.webFetchCount++;
                    break;
                case GroupAccumulator::MemorySearch:
                    acc.memorySearchCount++;
                    break;
                case GroupAccumulator::MemoryWrite:
                    acc.memoryWriteCount++;
                    break;
                case GroupAccumulator::MCP:
                    acc.mcpTotalCount++;
                    break;
            }

            // Track the latest display hint
            if (!block.summary.primaryText.empty()) {
                acc.latestDisplayHint = block.summary.primaryText;
            }

            i++;
            continue;
        }

        // Non-collapsible, non-breaking block — flush group, pass through
        flushGroup();
        result.push_back(std::move(block));
        i++;
    }

    // Flush any remaining group
    flushGroup();

    return result;
}

// ========== Group summary text generation ==========

String MessagePipeline::buildGroupSummary(const GroupAccumulator& acc, bool hasContentAfter) {
    return hasContentAfter ? buildActiveSummary(acc) : buildFinalizedSummary(acc);
}

String MessagePipeline::buildFinalizedSummary(const GroupAccumulator& acc) {
    std::vector<String> parts;
    // Use past tense

    if (acc.readCount() > 0) {
        parts.push_back("Read " + std::to_string(acc.readCount()) + " files");
    }
    if (acc.searchCount > 0) {
        parts.push_back("Searched for " + std::to_string(acc.searchCount) + " patterns");
    }
    if (acc.listCount > 0) {
        parts.push_back("Listed " + std::to_string(acc.listCount) + " directories");
    }
    if (acc.bashCount > 0) {
        parts.push_back("Ran " + std::to_string(acc.bashCount) + " commands");
    }
    if (acc.webSearchCount > 0) {
        parts.push_back("Searched web " + std::to_string(acc.webSearchCount) + " times");
    }
    if (acc.webFetchCount > 0) {
        parts.push_back("Fetched " + std::to_string(acc.webFetchCount) + " pages");
    }
    if (acc.memorySearchCount > 0) {
        parts.push_back("Recalled " + std::to_string(acc.memorySearchCount) + " memories");
    }
    if (acc.memoryWriteCount > 0) {
        parts.push_back("Wrote " + std::to_string(acc.memoryWriteCount) + " memories");
    }
    if (acc.mcpTotalCount > 0) {
        for (auto& [server, count] : acc.mcpServerCounts) {
            parts.push_back("Queried " + server + " " + std::to_string(count) + " times");
        }
    }

    if (parts.empty()) return "No operations";

    // Capitalize first verb
    if (!parts.empty() && !parts[0].empty()) {
        // Already capitalized from template
    }

    std::ostringstream oss;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) oss << " · ";
        oss << parts[i];
    }
    return oss.str();
}

String MessagePipeline::buildActiveSummary(const GroupAccumulator& acc) {
    std::vector<String> parts;
    // Use present continuous tense

    if (acc.readCount() > 0) {
        parts.push_back("Reading " + std::to_string(acc.readCount()) + " files");
    }
    if (acc.searchCount > 0) {
        parts.push_back("Searching for " + std::to_string(acc.searchCount) + " patterns");
    }
    if (acc.listCount > 0) {
        parts.push_back("Listing " + std::to_string(acc.listCount) + " directories");
    }
    if (acc.bashCount > 0) {
        parts.push_back("Running " + std::to_string(acc.bashCount) + " commands");
    }
    if (acc.webSearchCount > 0) {
        parts.push_back("Searching web " + std::to_string(acc.webSearchCount) + " times");
    }
    if (acc.webFetchCount > 0) {
        parts.push_back("Fetching " + std::to_string(acc.webFetchCount) + " pages");
    }
    if (acc.memorySearchCount > 0) {
        parts.push_back("Recalling " + std::to_string(acc.memorySearchCount) + " memories");
    }
    if (acc.memoryWriteCount > 0) {
        parts.push_back("Writing " + std::to_string(acc.memoryWriteCount) + " memories");
    }

    if (parts.empty()) return "Working";

    std::ostringstream oss;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) oss << " · ";
        oss << parts[i];
    }
    oss << "…";
    return oss.str();
}

// ========== Pass 5-7: Stub implementations ==========

std::vector<ContentBlock> MessagePipeline::collapseBackgroundBash(
    std::vector<ContentBlock> blocks) {
    // Collapse background bash notification pairs into single lines.
    // Background bash notifications appear as consecutive ToolResult blocks
    // where isBackgroundNotification == true, with a Start/End pair.
    //
    // Placeholder: full implementation requires BackgroundNotification
    // tracking in the tool executor. For now, pass through.
    return blocks;
}

std::vector<ContentBlock> MessagePipeline::collapseHookSummaries(
    std::vector<ContentBlock> blocks) {
    // Merge consecutive hook SystemMessage blocks into a summary.
    //
    // When N hooks fire for the same tool, they produce N SystemMessage blocks.
    // This pass collapses them into one: "Ran N PreToolUse hooks (Xs)".
    //
    // Placeholder: requires hook summary tracking infrastructure.
    return blocks;
}

std::vector<ContentBlock> MessagePipeline::collapseTeammateShutdowns(
    std::vector<ContentBlock> blocks) {
    // Collapse agent/teammate shutdown sequences into a single notification.
    //
    // When a sub-agent shuts down, it produces multiple AgentProgress +
    // SystemMessage blocks. This pass collapses them.
    //
    // Placeholder: requires agent lifecycle tracking.
    return blocks;
}

// ========== Pass 7: buildLookups ==========

void MessagePipeline::buildLookups(const std::vector<ContentBlock>& blocks) {
    lookups_ = MessageLookups{};

    for (auto& block : blocks) {
        // Track tool names by tool_use_id
        if (!block.toolCallId.empty() && !block.toolName.empty()) {
            lookups_.toolUseIdToToolName[block.toolCallId] = block.toolName;
        }

        // Track resolved tools (tool results)
        if (block.type == ContentBlock::ToolResult && !block.toolCallId.empty()) {
            lookups_.resolvedToolUseIds.insert(block.toolCallId);
            if (block.summary.isError) {
                lookups_.erroredToolUseIds.insert(block.toolCallId);
            }
        }

        // Track in-progress tools (ToolProgress without matching ToolResult)
        if (block.type == ContentBlock::ToolProgress && !block.toolCallId.empty()) {
            // Check if this progress has a matching result elsewhere
            bool hasResult = false;
            for (auto& other : blocks) {
                if (other.type == ContentBlock::ToolResult &&
                    other.toolCallId == block.toolCallId) {
                    hasResult = true;
                    break;
                }
            }
            if (!hasResult) {
                lookups_.inProgressToolUseIds.insert(block.toolCallId);
            }
        }

        // Track sibling tool_use_ids (tools in the same ToolGroup)
        if (block.type == ContentBlock::ToolGroup) {
            for (auto& id : block.toolUseIds) {
                auto& siblings = lookups_.siblingToolUseIds[id];
                for (auto& sid : block.toolUseIds) {
                    if (sid != id) siblings.insert(sid);
                }
            }
        }
    }
}

} // namespace claude
