#include "claude/core/ContentBlockParam.hpp"
#include <cstdlib>
#include <iostream>
#include <unordered_set>

namespace claude {

// ========== Diagnostic helpers (gated by CLAUDE_CODE_DEBUG_API_MESSAGES=1) ==========

static bool debugApiMessages() {
    const char* env = std::getenv("CLAUDE_CODE_DEBUG_API_MESSAGES");
    return env && env[0] == '1' && env[1] == '\0';
}

static const char* roleLabel(MessageRole r) {
    switch (r) {
        case MessageRole::System:    return "system";
        case MessageRole::User:      return "user";
        case MessageRole::Assistant: return "assistant";
        case MessageRole::ToolResult:return "tool_result";
    }
    return "?";
}

static void dumpContentHistory(const char* label,
                               const std::vector<ContentMessage>& msgs) {
    if (!debugApiMessages()) return;
    std::ostream& out = std::cerr;
    out << "\n--- " << label << " (" << msgs.size() << " messages) ---\n";
    for (size_t i = 0; i < msgs.size(); ++i) {
        const auto& m = msgs[i];
        out << "  [" << i << "] role=" << roleLabel(m.role)
            << " apiRound=" << m.apiRound
            << " blocks=" << m.content.size() << "\n";
        for (size_t j = 0; j < m.content.size(); ++j) {
            const auto& b = m.content[j];
            out << "      [" << j << "] type=";
            switch (b.type) {
                case ContentBlockParam::Text:
                    out << "text";
                    if (!b.text.empty()) {
                        String p = b.text.size() > 100
                            ? b.text.substr(0, 100) + "..." : b.text;
                        // Escape newlines for single-line output
                        for (auto& ch : p) if (ch == '\n') ch = ' ';
                        out << " preview=\"" << p << "\"";
                    }
                    break;
                case ContentBlockParam::ToolUse:
                    out << "tool_use id=" << b.id << " name=" << b.name;
                    break;
                case ContentBlockParam::ToolResult:
                    out << "tool_result tool_use_id=" << b.toolUseId
                        << " is_error=" << (b.isError ? "true" : "false");
                    {
                        String p = b.resultContent.size() > 80
                            ? b.resultContent.substr(0, 80) + "..." : b.resultContent;
                        for (auto& ch : p) if (ch == '\n') ch = ' ';
                        out << " preview=\"" << p << "\"";
                    }
                    break;
                case ContentBlockParam::Thinking:
                    out << "thinking len=" << b.thinking.size();
                    break;
                case ContentBlockParam::RedactedThinking:
                    out << "redacted_thinking len=" << b.redactedData.size();
                    break;
            }
            out << "\n";
        }
    }
}

Json serializeContentBlock(const ContentBlockParam& block, const String& provider) {
    Json j;
    switch (block.type) {
        case ContentBlockParam::Text:
            j = {{"type", "text"}, {"text", block.text}};
            break;
        case ContentBlockParam::ToolUse:
            if (provider == "anthropic") {
                j = {{"type", "tool_use"}, {"id", block.id},
                     {"name", block.name}, {"input", block.input}};
            } else {
                // OpenAI: "arguments" must be a JSON string, not an object
                j = {{"type", "function"}, {"id", block.id},
                     {"function", {{"name", block.name},
                                   {"arguments", block.input.dump()}}}};
            }
            break;
        case ContentBlockParam::ToolResult: {
            String content = block.resultContent;
            if (block.truncated) {
                content += "\n[truncated]";
            }
            j = {{"type", "tool_result"}, {"tool_use_id", block.toolUseId},
                 {"content", content}};
            if (block.isError) j["is_error"] = true;
            break;
        }
        case ContentBlockParam::Thinking:
            j = {{"type", "thinking"}, {"thinking", block.thinking},
                 {"signature", block.signature}};
            break;
        case ContentBlockParam::RedactedThinking:
            j = {{"type", "redacted_thinking"}, {"data", block.redactedData}};
            break;
    }
    return j;
}

Json serializeContentMessageForAnthropic(const ContentMessage& msg) {
    Json j;
    switch (msg.role) {
        case MessageRole::System:    j["role"] = "system"; break;
        case MessageRole::User:      j["role"] = "user"; break;
        case MessageRole::Assistant: j["role"] = "assistant"; break;
        case MessageRole::ToolResult: j["role"] = "user"; break;
    }
    Json contentArr = Json::array();
    for (auto& block : msg.content) {
        contentArr.push_back(serializeContentBlock(block, "anthropic"));
    }
    j["content"] = contentArr;
    return j;
}

Json buildAnthropicApiMessages(const std::vector<ContentMessage>& history) {
    Json messages = Json::array();
    for (size_t i = 0; i < history.size(); ++i) {
        auto& msg = history[i];
        if (msg.role == MessageRole::System) continue;

        auto serialized = serializeContentMessageForAnthropic(msg);

        // Merge consecutive tool-result-only messages (Anthropic requires tool_results in user messages,
        // and two consecutive user messages are invalid, so we merge when BOTH are tool-result-only)
        if (!messages.empty() && serialized["role"] == "user") {
            auto& last = messages.back();
            if (last["role"] == "user") {
                // Check if the previous message was tool-result-only
                bool lastAllToolResults = true;
                for (auto& b : last["content"]) {
                    if (b.value("type", "") != "tool_result") {
                        lastAllToolResults = false;
                        break;
                    }
                }
                // Check if current message is also tool-result-only
                bool curAllToolResults = true;
                for (auto& b : msg.content) {
                    if (b.type != ContentBlockParam::ToolResult) {
                        curAllToolResults = false;
                        break;
                    }
                }
                if (lastAllToolResults && curAllToolResults) {
                    for (auto& block : serialized["content"]) {
                        last["content"].push_back(block);
                    }
                    continue;
                }
            }
        }
        messages.push_back(serialized);
    }
    return messages;
}

Json serializeContentMessageForOpenAI(const ContentMessage& msg) {
    Json j;
    switch (msg.role) {
        case MessageRole::System:    j["role"] = "system"; break;
        case MessageRole::User:      j["role"] = "user"; break;
        case MessageRole::Assistant: j["role"] = "assistant"; break;
        case MessageRole::ToolResult: j["role"] = "tool"; break;
    }

    bool hasToolUse = false;
    for (auto& b : msg.content) {
        if (b.type == ContentBlockParam::ToolUse) { hasToolUse = true; break; }
    }

    if (hasToolUse) {
        j["content"] = msg.textContent();
        Json toolCalls = Json::array();
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolUse) {
                toolCalls.push_back(serializeContentBlock(block, "openai"));
            }
        }
        j["tool_calls"] = toolCalls;
    } else if (msg.role == MessageRole::ToolResult) {
        for (auto& block : msg.content) {
            if (block.type == ContentBlockParam::ToolResult) {
                j["tool_call_id"] = block.toolUseId;
                j["content"] = block.resultContent;
                break;
            }
        }
    } else {
        j["content"] = msg.textContent();
    }

    return j;
}

ContentMessage convertLegacyMessage(const Message& old) {
    ContentMessage result;
    result.role = old.role;
    result.timestamp = old.timestamp;
    result.apiRound = old.apiRound;

    if (old.thinking.has_value() && !old.thinking->empty()) {
        result.content.push_back(ContentBlockParam::makeThinking(
            *old.thinking, old.signature.value_or("")));
    }

    // When the legacy message carries toolResults, skip emitting msg.content as
    // a text block. Otherwise the text block lands before tool_result blocks in
    // the API message, violating the Anthropic constraint that a user message
    // after assistant tool_use must have tool_result blocks first.
    //
    // Two paths produce non-empty msg.content on tool-result-carrying messages:
    // 1. MicroCompact writes "[Old tool result content cleared...]" to both
    //    msg.content and each ToolResponse.content.
    // 2. PostCompactCleanup::enforceAlternation merges a regular User message
    //    with a ToolResult message, copying ToolResult's content.
    //
    // In both cases the tool_result blocks carry the essential information;
    // the text block is redundant at best, protocol-violating at worst.
    if (!old.content.empty() && old.toolResults.empty()) {
        result.content.push_back(ContentBlockParam::makeText(old.content));
    }

    for (auto& tc : old.toolCalls) {
        Json input = Json::object();
        try { input = Json::parse(tc.arguments); } catch (...) {}
        result.content.push_back(ContentBlockParam::makeToolUse(tc.id, tc.name, input));
    }

    for (auto& tr : old.toolResults) {
        auto block = ContentBlockParam::makeToolResult(tr.callId, tr.content, tr.isError);
        result.content.push_back(std::move(block));
    }

    for (auto& rt : old.redactedThinking) {
        result.content.push_back(ContentBlockParam::makeRedactedThinking(
            rt.value("data", rt.dump())));
    }

    return result;
}

void migrateLegacySession(Json& msg) {
    // Skip already-migrated format
    if (msg.value("_format_version", 0) >= 2) return;
    // Skip if content is already an array (new format)
    if (!msg.contains("content") || !msg["content"].is_string()) return;

    String flatContent = msg["content"].get<String>();
    auto role = msg.value("role", "");

    std::vector<Json> blocks;

    // 1. Thinking block (assistant only)
    if (role == "assistant" && msg.contains("thinking") && msg["thinking"].is_string()) {
        String thinkingText = msg["thinking"].get<String>();
        String signature = msg.value("signature", "");
        blocks.push_back({{"type", "thinking"}, {"thinking", thinkingText}, {"signature", signature}});
    }

    // 2. Text block
    if (!flatContent.empty()) {
        blocks.push_back({{"type", "text"}, {"text", flatContent}});
    }

    // 3. ToolUse blocks (assistant)
    if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
        for (auto& tc : msg["tool_calls"]) {
            Json input = tc.value("input", Json::object());
            if (input.is_string()) {
                try { input = Json::parse(input.get<String>()); } catch (...) { input = Json::object(); }
            }
            // Handle both {id, name, input} and OpenAI {id, function:{name,arguments}} formats
            String name = tc.value("name", "");
            if (name.empty() && tc.contains("function")) {
                name = tc["function"].value("name", "");
                String args = tc["function"].value("arguments", "");
                if (!args.empty()) {
                    try { input = Json::parse(args); } catch (...) { input = Json::object(); }
                }
            }
            blocks.push_back({{"type", "tool_use"},
                {"id", tc.value("id", "")},
                {"name", name},
                {"input", input}});
        }
    }

    // 4. ToolResult blocks
    if (msg.contains("tool_results") && msg["tool_results"].is_array()) {
        for (auto& tr : msg["tool_results"]) {
            Json block = {{"type", "tool_result"},
                {"tool_use_id", tr.value("tool_use_id", tr.value("tool_call_id", tr.value("call_id", "")))},
                {"content", tr.value("content", "")}};
            if (tr.value("is_error", false)) block["is_error"] = true;
            blocks.push_back(block);
        }
    }

    // 5. RedactedThinking blocks (assistant)
    if (role == "assistant" && msg.contains("redacted_thinking") && msg["redacted_thinking"].is_array()) {
        for (auto& rt : msg["redacted_thinking"]) {
            blocks.push_back({{"type", "redacted_thinking"}, {"data", rt.value("data", rt.dump())}});
        }
    }

    // Replace content, remove old fields
    msg["content"] = blocks;
    msg.erase("tool_calls");
    msg.erase("tool_results");
    msg.erase("thinking");
    msg.erase("signature");
    msg.erase("redacted_thinking");
    msg["_format_version"] = 2;
}

// ========== Protocol invariant enforcement ==========

std::vector<String> findOrphanedToolUses(const std::vector<ContentMessage>& messages) {
    std::vector<String> orphans;
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& current = messages[i];
        if (current.role != MessageRole::Assistant) continue;

        // Collect all tool_use IDs in this assistant message
        std::vector<String> toolUseIds;
        for (const auto& block : current.content) {
            if (block.type == ContentBlockParam::ToolUse) {
                toolUseIds.push_back(block.id);
            }
        }
        if (toolUseIds.empty()) continue;

        // Look for matching tool_results in subsequent messages before
        // the next assistant message (Anthropic protocol: tool_result must
        // immediately follow the assistant with tool_use)
        std::unordered_set<String> resultIds;
        for (size_t j = i + 1; j < messages.size(); ++j) {
            const auto& next = messages[j];
            if (next.role == MessageRole::Assistant) break;
            for (const auto& block : next.content) {
                if (block.type == ContentBlockParam::ToolResult) {
                    resultIds.insert(block.toolUseId);
                }
            }
        }

        for (const auto& id : toolUseIds) {
            if (resultIds.count(id) == 0) {
                orphans.push_back(id);
            }
        }
    }
    return orphans;
}

int injectMissingToolResults(std::vector<ContentMessage>& messages) {
    int injected = 0;

    dumpContentHistory("BEFORE_REPAIR", messages);

    // Process right-to-left so insertions at earlier indices don't
    // invalidate already-processed indices.
    for (ptrdiff_t i = static_cast<ptrdiff_t>(messages.size()) - 1; i >= 0; --i) {
        auto& current = messages[static_cast<size_t>(i)];
        if (current.role != MessageRole::Assistant) continue;

        // Collect tool_use IDs from this assistant
        std::vector<String> toolUseIds;
        for (const auto& block : current.content) {
            if (block.type == ContentBlockParam::ToolUse) {
                toolUseIds.push_back(block.id);
            }
        }
        if (toolUseIds.empty()) continue;

        // Check the IMMEDIATE next message.  If it already contains
        // ALL required tool_results (and no text before them), the
        // protocol is satisfied — nothing to repair.
        bool immediateCorrect = false;
        size_t k = static_cast<size_t>(i + 1);
        if (k < messages.size()) {
            const auto& nextMsg = messages[k];
            if (nextMsg.role == MessageRole::User ||
                nextMsg.role == MessageRole::ToolResult) {
                // Check that tool_result blocks come first
                bool hasTextBefore = false;
                std::unordered_set<String> nextIds;
                for (const auto& block : nextMsg.content) {
                    if (block.type == ContentBlockParam::ToolResult) {
                        if (hasTextBefore) break;  // text before tool_result → wrong order
                        nextIds.insert(block.toolUseId);
                    } else if (block.type == ContentBlockParam::Text &&
                               !block.text.empty()) {
                        hasTextBefore = true;
                    }
                }
                if (!hasTextBefore) {
                    bool allFound = true;
                    for (const auto& id : toolUseIds) {
                        if (nextIds.count(id) == 0) { allFound = false; break; }
                    }
                    if (allFound) immediateCorrect = true;
                }
            }
        }
        if (immediateCorrect) continue;

        // === REPAIR NEEDED ===
        // Collect existing tool_result blocks from subsequent messages
        // (before the next assistant), and identify missing IDs.
        std::unordered_map<String, ContentBlockParam> collectedResults;
        std::unordered_set<String> found;
        std::vector<size_t> removeIndices;
        size_t scanEnd = messages.size();
        for (size_t j = k; j < messages.size(); ++j) {
            if (messages[j].role == MessageRole::Assistant) {
                scanEnd = j;
                break;
            }
        }

        for (size_t j = k; j < scanEnd; ++j) {
            auto& msg = messages[j];
            auto& blocks = msg.content;
            // Collect tool_result blocks we need, remove them from their
            // original message.
            blocks.erase(
                std::remove_if(blocks.begin(), blocks.end(),
                    [&](const ContentBlockParam& block) {
                        if (block.type != ContentBlockParam::ToolResult) return false;
                        for (const auto& id : toolUseIds) {
                            if (block.toolUseId == id) {
                                collectedResults[id] = block;
                                found.insert(id);
                                return true;  // remove from original
                            }
                        }
                        return false;
                    }),
                blocks.end());
            // If message has no meaningful content left, mark for removal
            bool hasContent = false;
            for (const auto& b : msg.content) {
                if (b.type == ContentBlockParam::Text && !b.text.empty()) {
                    hasContent = true; break;
                }
                if (b.type == ContentBlockParam::ToolResult) {
                    hasContent = true; break;
                }
                if (b.type == ContentBlockParam::Thinking) {
                    hasContent = true; break;
                }
                if (b.type == ContentBlockParam::RedactedThinking) {
                    hasContent = true; break;
                }
            }
            if (!hasContent) {
                removeIndices.push_back(j);
            }
        }

        // Build the consolidated tool_result message
        ContentMessage consolidated;
        consolidated.role = MessageRole::ToolResult;
        int syntheticCount = 0;
        for (const auto& id : toolUseIds) {
            if (found.count(id)) {
                consolidated.content.push_back(collectedResults[id]);
            } else {
                consolidated.content.push_back(ContentBlockParam::makeToolResult(
                    id, "Interrupted: tool execution was cancelled",
                    /*isError=*/true));
                syntheticCount++;
            }
        }

        // Insert consolidated immediately after the assistant
        messages.insert(messages.begin() + i + 1, std::move(consolidated));

        // Remove emptied messages (process highest index first to preserve
        // lower indices, and adjust for the +1 shift from the insertion)
        std::sort(removeIndices.begin(), removeIndices.end(), std::greater<size_t>());
        for (size_t rmIdx : removeIndices) {
            messages.erase(messages.begin() +
                           static_cast<ptrdiff_t>(rmIdx + 1));  // +1 for the insertion shift
        }

        injected += syntheticCount;
        if (debugApiMessages()) {
            std::ostream& out = std::cerr;
            out << "REPAIR_ACTIONS: assistant[" << i << "] has " << toolUseIds.size()
                << " tool_use(s):";
            for (auto& id : toolUseIds) out << " " << id;
            out << "\n";
            out << "  moved_tool_result_ids:";
            for (auto& id : toolUseIds)
                if (found.count(id)) out << " " << id;
            if (found.empty()) out << " (none)";
            out << "\n";
            out << "  synthesized_tool_result_ids:";
            for (auto& id : toolUseIds)
                if (!found.count(id)) out << " " << id;
            if (syntheticCount == 0) out << " (none)";
            out << "\n";
            out << "  inserted_at_index: " << (i + 1) << "\n";
            if (!removeIndices.empty()) {
                out << "  removed_emptied_indices:";
                for (auto ri : removeIndices) out << " " << ri;
                out << "\n";
            }
        }
        if (syntheticCount > 0) {
            fprintf(stderr, "[WARN] ContentBlockParam: repaired %zu tool_use(s) at "
                    "message %td (%zu synthetic, %zu collected)\n",
                    toolUseIds.size(), i, static_cast<size_t>(syntheticCount),
                    found.size());
        }
    }

    dumpContentHistory("AFTER_REPAIR", messages);
    return injected;
}

// ========== Hard validator: reject protocol violations after repair ==========

bool validateToolResultOrdering(const std::vector<ContentMessage>& messages) {
    if (debugApiMessages()) {
        std::cerr << "VALIDATION_RESULT: running...\n";
    }
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role != MessageRole::Assistant) continue;

        // Collect tool_use IDs
        std::unordered_set<String> toolUseIds;
        for (const auto& block : messages[i].content) {
            if (block.type == ContentBlockParam::ToolUse) {
                toolUseIds.insert(block.id);
            }
        }
        if (toolUseIds.empty()) continue;

        // Must have a next message
        if (i + 1 >= messages.size()) {
            auto msg = "assistant[" + std::to_string(i) + "] has tool_use but no following message";
            fprintf(stderr, "[ERROR] Protocol: %s\n", msg.c_str());
            if (debugApiMessages()) std::cerr << "VALIDATION_RESULT: FAIL — " << msg << "\n";
            return false;
        }

        const auto& next = messages[i + 1];
        if (next.role != MessageRole::User &&
            next.role != MessageRole::ToolResult) {
            auto msg = "assistant[" + std::to_string(i) + "] next message role is "
                     + String(roleLabel(next.role)) + " (expected user or tool_result)";
            fprintf(stderr, "[ERROR] Protocol: %s\n", msg.c_str());
            if (debugApiMessages()) std::cerr << "VALIDATION_RESULT: FAIL — " << msg << "\n";
            return false;
        }

        // Next message must start with tool_result blocks covering all IDs
        std::unordered_set<String> resultIds;
        for (const auto& block : next.content) {
            if (block.type == ContentBlockParam::ToolResult) {
                resultIds.insert(block.toolUseId);
            } else if (block.type == ContentBlockParam::Text &&
                       !block.text.empty() && resultIds.empty()) {
                // Plain text before any tool_result → violation
                String preview = block.text.size() > 60
                    ? block.text.substr(0, 60) + "..." : block.text;
                auto msg = "assistant[" + std::to_string(i)
                    + "] has tool_use but next message[" + std::to_string(i+1)
                    + "] starts with text (not tool_result): \"" + preview + "\"";
                fprintf(stderr, "[ERROR] Protocol: %s\n", msg.c_str());
                if (debugApiMessages()) std::cerr << "VALIDATION_RESULT: FAIL — " << msg << "\n";
                return false;
            }
        }

        for (const auto& id : toolUseIds) {
            if (resultIds.count(id) == 0) {
                auto msg = "assistant[" + std::to_string(i) + "] orphan tool_use "
                         + id + " — no matching tool_result in next message";
                fprintf(stderr, "[ERROR] Protocol: %s\n", msg.c_str());
                if (debugApiMessages()) std::cerr << "VALIDATION_RESULT: FAIL — " << msg << "\n";
                return false;
            }
        }
    }
    if (debugApiMessages()) std::cerr << "VALIDATION_RESULT: PASS\n";
    return true;
}

} // namespace claude
