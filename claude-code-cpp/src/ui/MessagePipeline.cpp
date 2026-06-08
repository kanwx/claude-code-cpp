#include <claude/ui/MessagePipeline.hpp>
#include <claude/ui/XmlTagDispatcher.hpp>
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

bool isToolResultSubtype(claude::DisplayMessage::Type type) {
    return type == claude::DisplayMessage::Type::UserToolResult
        || type == claude::DisplayMessage::Type::UserToolSuccess
        || type == claude::DisplayMessage::Type::UserToolError
        || type == claude::DisplayMessage::Type::UserToolRejected
        || type == claude::DisplayMessage::Type::UserToolCanceled;
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
            pendingThinkingText_.clear();
            pendingToolUseIndex_.clear();
            return false; // No message change yet

        case StreamEvent::Type::TextDelta:
            isStreaming_ = true;
            isThinking_ = false;
            streamingText_ += event.text;
            return true; // Streaming text changed

        case StreamEvent::Type::ThinkingDelta:
            thinkingSummary_ += event.text;
            pendingThinkingText_ += event.text;
            // Keep summary trimmed to last 60 chars
            if (thinkingSummary_.size() > 60) {
                thinkingSummary_ = "..." + thinkingSummary_.substr(thinkingSummary_.size() - 57);
            }
            return true;

        case StreamEvent::Type::ToolUseStart: {
            // Flush accumulated thinking text before tool event (ensures inline order:
            // thinking → text → tool_use, not tool_use → thinking → text)
            if (!pendingThinkingText_.empty()) {
                auto thinkMsg = DisplayMessage::assistantThinking(std::move(pendingThinkingText_), /*collapsed=*/true);
                thinkMsg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(thinkMsg));
                pendingThinkingText_.clear();
                isThinking_ = false;
            }
            // Flush accumulated streaming text before tool event (ensures inline order:
            // text-so-far → tool_use, not tool_use → text)
            if (!streamingText_.empty()) {
                auto textMsg = DisplayMessage::assistantText(std::move(streamingText_));
                textMsg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(textMsg));
                streamingText_.clear();
            }

            auto msg = DisplayMessage::assistantToolUse(
                ToolUseBlock{event.toolId, event.toolName, event.toolInput});
            msg.messageId = MessageIdGenerator::next();
            messages.push_back(std::move(msg));
            pendingToolUseIndex_[event.toolId] = messages.size() - 1;
            return true;
        }

        case StreamEvent::Type::ToolUseComplete: {
            // Flush streaming text before updating tool_use (maintains inline order)
            if (!streamingText_.empty()) {
                auto textMsg = DisplayMessage::assistantText(std::move(streamingText_));
                textMsg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(textMsg));
                streamingText_.clear();
            }

            // Update the tool_use message with complete input
            auto it = pendingToolUseIndex_.find(event.toolId);
            if (it != pendingToolUseIndex_.end() && it->second < messages.size()) {
                messages[it->second].toolUse.input = event.toolInput;
            }
            return true;
        }

        case StreamEvent::Type::ToolResultReady: {
            // Flush any accumulated streaming text before tool result.
            // This ensures the order: text-before-tool → tool_result → text-after-tool
            // rather than tool_result appearing before text that preceded it.
            if (!streamingText_.empty()) {
                auto textMsg = DisplayMessage::assistantText(std::move(streamingText_));
                textMsg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(textMsg));
                streamingText_.clear();
            }

            // Find paired tool_use to get input for denormalization
            String toolInput;
            auto it = pendingToolUseIndex_.find(event.toolId);
            if (it != pendingToolUseIndex_.end() && it->second < messages.size()) {
                toolInput = messages[it->second].toolUse.input;
            }

            DisplayMessage resultMsg;
            if (event.toolIsRejected) {
                resultMsg = DisplayMessage::userToolRejected(event.toolId, event.toolName, toolInput);
            } else if (event.toolIsCancelled) {
                resultMsg = DisplayMessage::userToolCanceled(event.toolId, event.toolName, toolInput);
            } else if (event.toolIsError) {
                resultMsg = DisplayMessage::userToolError(event.toolId, event.toolName, event.toolResult, toolInput);
            } else {
                resultMsg = DisplayMessage::userToolSuccess(event.toolId, event.toolName, event.toolResult, toolInput);
            }
            messages.push_back(std::move(resultMsg));
            // Mark the tool_use as having its result
            if (it != pendingToolUseIndex_.end()) {
                pendingToolUseIndex_.erase(it);
            }
            return true;
        }

        case StreamEvent::Type::ToolChunkReady: {
            // Incremental tool output — append to pending streaming text
            // The final ToolResultReady will commit the complete output.
            // For now, accumulate for progressive rendering.
            streamingChunkText_ += event.text;
            return true;
        }

        case StreamEvent::Type::StreamEnd: {
            isStreaming_ = false;
            isThinking_ = false;

            // Commit accumulated thinking as AssistantThinking DisplayMessage
            if (!pendingThinkingText_.empty()) {
                auto thinkMsg = DisplayMessage::assistantThinking(std::move(pendingThinkingText_), /*collapsed=*/true);
                thinkMsg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(thinkMsg));
                pendingThinkingText_.clear();
            }

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
            auto dispatchType = claude::ui::XmlTagDispatcher::dispatch(event.text);
            if (dispatchType != DisplayMessage::Type::UserPrompt) {
                DisplayMessage msg;
                msg.type = dispatchType;
                msg.messageId = MessageIdGenerator::next();
                auto parsed = claude::ui::XmlTagDispatcher::parseFirstTag(event.text);
                msg.text = parsed ? parsed->content : event.text;
                msg.timestamp = std::chrono::steady_clock::now();
                messages.push_back(std::move(msg));
            } else {
                auto msg = DisplayMessage::userPrompt(event.text);
                msg.messageId = MessageIdGenerator::next();
                messages.push_back(std::move(msg));
            }
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
        case StreamEvent::Type::ToolChunkReady:
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

/// Check if a message type is a non-tool "boundary" message that should
/// break tool group sequences. Non-tool messages must not be crossed
/// when grouping, or the display order will be wrong.
static bool isGroupBoundary(const DisplayMessage& m) {
    switch (m.type) {
        case DisplayMessage::Type::AssistantToolUse:
        case DisplayMessage::Type::UserToolResult:
        case DisplayMessage::Type::UserToolSuccess:
        case DisplayMessage::Type::UserToolError:
        case DisplayMessage::Type::UserToolRejected:
        case DisplayMessage::Type::UserToolCanceled:
            return false; // Tool-related — not a boundary
        default:
            return true;  // Everything else is a group boundary
    }
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
    // IMPORTANT: Non-tool messages (AssistantText, SystemInfo, etc.) act as
    // group boundaries. Tools separated by a boundary must NOT be collapsed
    // together, or they will appear out of order relative to the boundary message.
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
                    const auto& tmsg = messages[i];
                    groupMsgs.push_back(messages[i]);
                    group.toolIndices.push_back(i - groupStart + result.size());

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

                    // Collect paired tool_result into groupMsgs (do not skip/drop it)
                    if (i < messages.size() &&
                        isToolResultSubtype(messages[i].type) &&
                        messages[i].toolResult.toolUseId == tmsg.toolUse.toolId) {
                        groupMsgs.push_back(messages[i]);
                        i++;
                    }
                } else if (i < messages.size() && isToolResultSubtype(messages[i].type)) {
                    // Stray tool_result not paired with the last tool_use —
                    // include it in the group so it's not lost
                    groupMsgs.push_back(messages[i]);
                    i++;
                } else {
                    // Non-tool message (AssistantText, etc.) — this is a group boundary.
                    // Stop grouping here so tools before and after the boundary
                    // remain in their correct display positions.
                    break;
                }
            }

            // Only collapse if 2+ tool_use messages (not counting tool_results)
            size_t toolUseCount = 0;
            for (const auto& m : groupMsgs) {
                if (m.type == DisplayMessage::Type::AssistantToolUse) toolUseCount++;
            }
            if (toolUseCount >= 2) {
                auto collapsed = DisplayMessage::collapsedReadSearch(std::move(group));
                collapsed.messageId = MessageIdGenerator::next();
                result.push_back(std::move(collapsed));
            } else {
                // Just 1 tool (or 1 tool_use + 1 tool_result): keep as-is
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
    // Same boundary rule applies: only group consecutive tool_uses with no
    // intervening non-tool messages.
    {
        std::vector<DisplayMessage> final;
        final.reserve(result.size());
        size_t j = 0;
        while (j < result.size()) {
            if (result[j].type == DisplayMessage::Type::AssistantToolUse) {
                String toolName = result[j].toolUse.toolName;
                std::vector<ToolUseRenderData> group;
                std::vector<DisplayMessage> ungrouped; // fallback if only 1
                size_t start = j;

                while (j < result.size() &&
                       result[j].type == DisplayMessage::Type::AssistantToolUse &&
                       result[j].toolUse.toolName == toolName) {
                    ToolUseRenderData rd;
                    rd.toolUseId = result[j].toolUse.toolId;
                    rd.toolName = result[j].toolUse.toolName;
                    rd.arguments = result[j].toolUse.input;
                    rd.isInProgress = false;
                    ungrouped.push_back(result[j]);
                    j++;

                    // Collect paired tool_result
                    if (j < result.size() &&
                        isToolResultSubtype(result[j].type)) {
                        rd.result = result[j].toolResult.result;
                        rd.isError = result[j].toolResult.isError;
                        ungrouped.push_back(result[j]);
                        j++;
                    }
                    group.push_back(std::move(rd));
                }

                if (group.size() >= 2) {
                    auto gmsg = DisplayMessage::groupedToolUse(std::move(group));
                    gmsg.messageId = MessageIdGenerator::next();
                    final.push_back(std::move(gmsg));
                } else {
                    for (auto& m : ungrouped) {
                        final.push_back(std::move(m));
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
