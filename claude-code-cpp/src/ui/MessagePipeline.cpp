#include <claude/ui/MessagePipeline.hpp>
#include <spdlog/spdlog.h>

namespace {

// ========== Tool Classification Helpers ==========

using claude::String;

enum class ToolCategory { Read, Search, List, Bash, Memory, Hook, MCP, Other };

ToolCategory classifyTool(const String& toolName) {
    if (toolName == "Read" || toolName == "FileReadTool") return ToolCategory::Read;
    if (toolName == "Grep" || toolName == "GrepTool") return ToolCategory::Search;
    if (toolName == "Glob" || toolName == "GlobTool") return ToolCategory::Search;
    if (toolName == "LS" || toolName == "ListTool") return ToolCategory::List;
    if (toolName == "Bash" || toolName == "BashTool") return ToolCategory::Bash;
    if (toolName == "MemoryTool" || toolName.find("memory_") == 0) return ToolCategory::Memory;
    if (toolName.find("mcp__") == 0) return ToolCategory::MCP;
    return ToolCategory::Other;
}

String extractJsonField(const String& json, const String& field) {
    // Quick string extraction for JSON field value (avoids full parse overhead)
    size_t pos = json.find("\"" + field + "\"");
    if (pos == String::npos) return "";
    size_t q1 = json.find('"', pos + field.size() + 2);
    if (q1 == String::npos) return "";
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == String::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

/// Check if a tool category should be collapsed into a CollapsedReadSearch group.
bool isCollapsibleCategory(ToolCategory cat) {
    return cat == ToolCategory::Read || cat == ToolCategory::Search ||
           cat == ToolCategory::List || cat == ToolCategory::Memory;
}

} // anonymous namespace

namespace claude {

// ========== NormalizeStage ==========

bool NormalizeStage::processEvent(const StreamEvent& event,
                                  std::vector<DisplayMessage>& messages) {
    switch (event.type) {
        case StreamEvent::Type::StreamStart:
            isStreaming_ = true;
            isThinking_ = true;
            streamingText_.clear();
            thinkingSummary_.clear();
            pendingToolUseIndex_.clear();
            return false; // No message change yet

        case StreamEvent::Type::TextDelta:
            isStreaming_ = true;
            isThinking_ = false;
            streamingText_ += event.text;
            return true; // Streaming text changed

        case StreamEvent::Type::ThinkingDelta:
            thinkingSummary_ += event.text;
            // Keep summary trimmed to last 60 chars
            if (thinkingSummary_.size() > 60) {
                thinkingSummary_ = "..." + thinkingSummary_.substr(thinkingSummary_.size() - 57);
            }
            return true;

        case StreamEvent::Type::ToolUseStart: {
            auto msg = DisplayMessage::assistantToolUse(
                ToolUseBlock{event.toolId, event.toolName, event.toolInput});
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            pendingToolUseIndex_[event.toolId] = messages.size() - 1;
            return true;
        }

        case StreamEvent::Type::ToolUseComplete: {
            // Update the tool_use message with complete input
            auto it = pendingToolUseIndex_.find(event.toolId);
            if (it != pendingToolUseIndex_.end() && it->second < messages.size()) {
                messages[it->second].toolUse.input = event.toolInput;
            }
            return true;
        }

        case StreamEvent::Type::ToolResultReady: {
            // Find matching tool_use and add result message
            auto resultMsg = DisplayMessage::userToolResult(
                ToolResultBlock{event.toolId, event.toolName, event.toolResult, event.toolIsError});
            resultMsg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(resultMsg));
            // Mark the tool_use as having its result
            auto it = pendingToolUseIndex_.find(event.toolId);
            if (it != pendingToolUseIndex_.end()) {
                pendingToolUseIndex_.erase(it);
            }
            return true;
        }

        case StreamEvent::Type::StreamEnd: {
            isStreaming_ = false;
            isThinking_ = false;

            // Commit streaming text as final assistant message
            if (!streamingText_.empty()) {
                auto msg = DisplayMessage::assistantText(std::move(streamingText_));
                msg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(msg));
                streamingText_.clear();
            }

            if (!event.success) {
                auto emsg = DisplayMessage::systemError(event.text);
                emsg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(emsg));
            }
            return true;
        }

        case StreamEvent::Type::StreamError: {
            auto emsg = DisplayMessage::systemError(event.text);
            emsg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(emsg));
            return true;
        }

        case StreamEvent::Type::UserMessage: {
            auto msg = DisplayMessage::userPrompt(event.text);
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            return true;
        }

        case StreamEvent::Type::SystemMessage: {
            auto msg = DisplayMessage::systemInfo(event.text);
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            return true;
        }

        case StreamEvent::Type::ErrorMessage: {
            auto msg = DisplayMessage::systemError(event.text);
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            return true;
        }

        case StreamEvent::Type::TurnDuration: {
            auto msg = DisplayMessage::turnDuration(event.text);
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            return true;
        }

        case StreamEvent::Type::CompactBoundary: {
            auto msg = DisplayMessage::systemInfo("── context compacted ──");
            msg.type = DisplayMessage::Type::CompactBoundary;
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            return true;
        }

        case StreamEvent::Type::HookSummary: {
            auto msg = DisplayMessage::systemInfo(event.text);
            msg.type = DisplayMessage::Type::HookSummary;
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            return true;
        }

        case StreamEvent::Type::Tombstone:
            return false;
    }
    return false;
}

// ========== GroupStage ==========

bool GroupStage::processEvent(const StreamEvent& event,
                              std::vector<DisplayMessage>& messages) {
    switch (event.type) {
        case StreamEvent::Type::ToolResultReady:
        case StreamEvent::Type::StreamEnd:
            needsRegroup_ = true;
            break;
        default:
            break;
    }

    if (needsRegroup_) {
        applyGrouping(messages);
        needsRegroup_ = false;
        return true;
    }
    return false;
}

void GroupStage::regroupAll(std::vector<DisplayMessage>& messages) {
    applyGrouping(messages);
}

void GroupStage::applyGrouping(std::vector<DisplayMessage>& messages) {
    // Remove existing CollapsedReadSearch and GroupedToolUse messages (they'll be regenerated)
    messages.erase(
        std::remove_if(messages.begin(), messages.end(),
            [](const DisplayMessage& m) {
                return m.type == DisplayMessage::Type::CollapsedReadSearch ||
                       m.type == DisplayMessage::Type::GroupedToolUse;
            }),
        messages.end());

    if (verboseMode_) return; // Don't collapse in verbose mode

    // ====== Pass 1: Collapse consecutive read/search/list/memory tools ======
    std::vector<DisplayMessage> result;
    result.reserve(messages.size());

    size_t i = 0;
    while (i < messages.size()) {
        // Check if this starts a sequence of collapsible tools
        if (messages[i].type == DisplayMessage::Type::AssistantToolUse &&
            isCollapsibleCategory(classifyTool(messages[i].toolUse.toolName))) {
            CollapsedToolGroup group;
            group.active = false;
            std::vector<DisplayMessage> groupMsgs;
            size_t groupStart = i;

            while (i < messages.size()) {
                if (messages[i].type == DisplayMessage::Type::AssistantToolUse &&
                    isCollapsibleCategory(classifyTool(messages[i].toolUse.toolName))) {
                    groupMsgs.push_back(messages[i]);
                    group.toolIndices.push_back(i - groupStart + result.size());

                    const auto& tmsg = messages[i];
                    auto cat = classifyTool(tmsg.toolUse.toolName);

                    switch (cat) {
                        case ToolCategory::Read:
                            group.readCount++;
                            {
                                String path = extractJsonField(tmsg.toolUse.input, "file_path");
                                if (path.empty()) path = extractJsonField(tmsg.toolUse.input, "path");
                                if (!path.empty()) {
                                    group.readFilePaths.push_back(path);
                                    group.latestHint = path;
                                }
                            }
                            break;
                        case ToolCategory::Search:
                            group.searchCount++;
                            {
                                String pattern = extractJsonField(tmsg.toolUse.input, "pattern");
                                if (pattern.empty()) pattern = extractJsonField(tmsg.toolUse.input, "query");
                                if (pattern.empty()) pattern = extractJsonField(tmsg.toolUse.input, "glob");
                                if (!pattern.empty()) {
                                    group.searchPatterns.push_back(pattern);
                                    group.latestHint = pattern;
                                }
                            }
                            break;
                        case ToolCategory::List:
                            group.listCount++;
                            break;
                        case ToolCategory::Memory:
                            group.memoryCount++;
                            break;
                        case ToolCategory::Bash:
                            group.bashCount++;
                            break;
                        case ToolCategory::MCP:
                            group.mcpCallCount++;
                            break;
                        case ToolCategory::Hook:
                            group.hookCount++;
                            break;
                        case ToolCategory::Other:
                            break;
                    }
                    i++;

                    // Skip paired tool_result
                    if (i < messages.size() &&
                        messages[i].type == DisplayMessage::Type::UserToolResult &&
                        messages[i].toolResult.toolUseId == tmsg.toolUse.toolId) {
                        i++;
                    }
                } else {
                    break;
                }
            }

            // Only collapse if 2+ tools
            if (groupMsgs.size() >= 2) {
                auto collapsed = DisplayMessage::collapsedReadSearch(std::move(group));
                collapsed.messageId = MessageIdGenerator::next();
                result.push_back(std::move(collapsed));
            } else {
                // Just 1 tool: keep as-is
                for (auto& m : groupMsgs) {
                    result.push_back(std::move(m));
                }
            }
        } else {
            result.push_back(messages[i]);
            i++;
        }
    }

    // ====== Pass 2: Merge consecutive same-type non-collapsed tool_use into GroupedToolUse ======
    {
        std::vector<DisplayMessage> final;
        final.reserve(result.size());
        size_t j = 0;
        while (j < result.size()) {
            if (result[j].type == DisplayMessage::Type::AssistantToolUse) {
                String toolName = result[j].toolUse.toolName;
                std::vector<ToolUseRenderData> group;
                size_t start = j;

                while (j < result.size() &&
                       result[j].type == DisplayMessage::Type::AssistantToolUse &&
                       result[j].toolUse.toolName == toolName) {
                    ToolUseRenderData rd;
                    rd.toolUseId = result[j].toolUse.toolId;
                    rd.toolName = result[j].toolUse.toolName;
                    rd.arguments = result[j].toolUse.input;
                    rd.isInProgress = false;
                    group.push_back(std::move(rd));
                    j++;

                    // Skip paired tool_result
                    if (j < result.size() &&
                        result[j].type == DisplayMessage::Type::UserToolResult) {
                        group.back().result = result[j].toolResult.result;
                        group.back().isError = result[j].toolResult.isError;
                        j++;
                    }
                }

                if (group.size() >= 2) {
                    auto gmsg = DisplayMessage::groupedToolUse(std::move(group));
                    gmsg.messageId = MessageIdGenerator::next();
                    final.push_back(std::move(gmsg));
                } else {
                    for (size_t k = start; k < j; ++k) {
                        final.push_back(std::move(result[k]));
                    }
                }
            } else {
                final.push_back(std::move(result[j]));
                j++;
            }
        }
        result = std::move(final);
    }

    messages = std::move(result);
}

// ========== CollapseStage ==========

bool CollapseStage::processEvent(const StreamEvent& event,
                                 std::vector<DisplayMessage>& messages) {
    switch (event.type) {
        case StreamEvent::Type::SystemMessage:
        case StreamEvent::Type::StreamEnd:
            deduplicateSystemMessages(messages);
            return true;
        default:
            return false;
    }
}

void CollapseStage::deduplicateSystemMessages(std::vector<DisplayMessage>& messages) {
    // Remove consecutive duplicate SystemInfo messages (e.g. background task spam)
    auto it = std::unique(messages.begin(), messages.end(),
        [](const DisplayMessage& a, const DisplayMessage& b) {
            if (a.type != DisplayMessage::Type::SystemInfo ||
                b.type != DisplayMessage::Type::SystemInfo) return false;
            return a.text == b.text;
        });
    if (it != messages.end()) {
        messages.erase(it, messages.end());
    }
}

// ========== MessagePipeline ==========

MessagePipeline::MessagePipeline()
    : normalizeStage_(std::make_unique<NormalizeStage>())
    , groupStage_(std::make_unique<GroupStage>())
    , collapseStage_(std::make_unique<CollapseStage>()) {
}

bool MessagePipeline::processEvent(const StreamEvent& event) {
    bool changed = false;

    // Stage 1: Normalize (produces DisplayMessages in rawMessages_)
    if (normalizeStage_->processEvent(event, rawMessages_)) {
        changed = true;
    }

    // Stage 2: Group (collapses read/search sequences)
    if (groupStage_->processEvent(event, rawMessages_)) {
        changed = true;
    }

    // Stage 3: Collapse (deduplication)
    if (collapseStage_->processEvent(event, rawMessages_)) {
        changed = true;
    }

    if (changed) {
        displayMessages_ = rawMessages_; // Copy for rendering
    }

    return changed;
}

void MessagePipeline::reprocess() {
    groupStage_->regroupAll(rawMessages_);
    collapseStage_->deduplicateSystemMessages(rawMessages_);
    displayMessages_ = rawMessages_;
}

} // namespace claude
