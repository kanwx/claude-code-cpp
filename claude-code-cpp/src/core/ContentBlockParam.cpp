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

void dumpContentHistory(const char* label,
                       const std::vector<ContentMessage>& msgs) {
    if (!debugApiMessages()) return;
    std::ostream& out = std::cerr;
    out << "\n--- " << label << " (" << msgs.size() << " messages) ---\n";
    for (size_t i = 0; i < msgs.size(); ++i) {
        const auto& m = msgs[i];

        // Collect tool_use/tool_result IDs for compact summary
        std::vector<String> toolUseIds, toolResultIds;
        size_t textLen = 0, trimmedTextLen = 0;
        bool hasEmptyBlock = false;
        for (const auto& b : m.content) {
            switch (b.type) {
                case ContentBlockParam::Text:
                    textLen += b.text.size();
                    { String t = b.text; while (!t.empty() && t.back() == ' ') t.pop_back();
                      while (!t.empty() && t.front() == ' ') t.erase(0, 1);
                      trimmedTextLen += t.size(); }
                    if (b.text.empty()) hasEmptyBlock = true;
                    break;
                case ContentBlockParam::ToolUse:
                    toolUseIds.push_back(b.id);
                    break;
                case ContentBlockParam::ToolResult:
                    toolResultIds.push_back(b.toolUseId);
                    if (b.resultContent.empty()) hasEmptyBlock = true;
                    break;
                default: break;
            }
        }

        // Compact summary line
        out << "idx=" << i << " role=" << roleLabel(m.role)
            << " blocks=" << m.content.size()
            << " textLen=" << textLen
            << " trimmedTextLen=" << trimmedTextLen;
        if (m.content.empty()) out << " EMPTY";
        if (hasEmptyBlock) out << " HAS_EMPTY_BLOCK";
        if (!toolUseIds.empty()) {
            out << " toolUseIds=[";
            for (size_t k = 0; k < toolUseIds.size(); ++k) {
                if (k) out << ", ";
                out << toolUseIds[k];
            }
            out << "]";
        }
        if (!toolResultIds.empty()) {
            out << " toolResultIds=[";
            for (size_t k = 0; k < toolResultIds.size(); ++k) {
                if (k) out << ", ";
                out << toolResultIds[k];
            }
            out << "]";
        }
        out << "\n";

        // Detailed block listing (abbreviated)
        for (size_t j = 0; j < m.content.size(); ++j) {
            const auto& b = m.content[j];
            out << "    block[" << j << "]=";
            switch (b.type) {
                case ContentBlockParam::Text: {
                    out << "text";
                    String preview = b.text.size() > 80
                        ? b.text.substr(0, 80) + "..." : b.text;
                    for (auto& ch : preview) if (ch == '\n') ch = '\\';
                    out << " len=" << b.text.size()
                        << (b.text.empty() ? " EMPTY" : "")
                        << " preview=\"" << preview << "\"";
                    break;
                }
                case ContentBlockParam::ToolUse:
                    out << "tool_use id=" << b.id << " name=" << b.name;
                    break;
                case ContentBlockParam::ToolResult:
                    out << "tool_result tool_use_id=" << b.toolUseId
                        << " isError=" << (b.isError ? "true" : "false")
                        << " len=" << b.resultContent.size()
                        << (b.resultContent.empty() ? " EMPTY" : "");
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
            if (content.empty()) {
                content = "(no output)";
            }
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
                String content = block.resultContent;
                if (content.empty()) {
                    content = "(no output)";
                }
                j["content"] = content;
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

    // When the legacy message carries toolResults AND is a ToolResult-role
    // message (MicroCompact placeholder), skip emitting msg.content as a text
    // block.  The tool_result blocks carry the essential information.
    //
    // When the message is User-role WITH toolResults (merged by our repair
    // persistence), emit text AFTER toolResults so the tool_result-first
    // ordering is preserved AND real user text is not lost.
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

    // Emit text AFTER toolResults for User-role messages with merged
    // tool results.  Tool_results must come before text per Anthropic
    // protocol.  Skip ToolResult-role messages (MicroCompact placeholder).
    if (!old.content.empty() && !old.toolResults.empty() &&
        old.role != MessageRole::ToolResult) {
        result.content.push_back(ContentBlockParam::makeText(old.content));
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
        // Standalone ToolResult messages serialize as "user", so if the next
        // message has ToolResult role AND a User message follows, merging the
        // tool_results into that User prevents consecutive users in the API JSON.
        if (immediateCorrect && messages[k].role == MessageRole::ToolResult) {
            // Check if there's a following User message to merge into
            bool hasFollowingUserText = false;
            for (size_t fj = k + 1; fj < messages.size(); ++fj) {
                if (messages[fj].role == MessageRole::Assistant) break;
                if (messages[fj].role == MessageRole::User) {
                    // Any non-tool_result block (text, thinking, etc.) means
                    // this user message would create consecutive users.
                    for (auto& b : messages[fj].content) {
                        if (b.type != ContentBlockParam::ToolResult &&
                            !b.text.empty()) {
                            hasFollowingUserText = true;
                            break;
                        }
                    }
                    break;
                }
            }
            if (hasFollowingUserText) {
                immediateCorrect = false;
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

        // Determine whether to merge into the existing next message or insert
        // standalone.  Inserting a standalone user(tool_result) creates
        // consecutive user messages when the next message is already user(text),
        // which violates Anthropic's role-alternation requirement.
        bool merged = false;
        // Find the first non-removed message after the assistant to potentially
        // merge tool_results into.  When the immediate next message (k) is in
        // removeIndices (e.g. a standalone ToolResult whose blocks were collected),
        // look further ahead for a User message to absorb the results.
        size_t mergeTarget = k;
        while (mergeTarget < messages.size()) {
            bool targetRemoved = false;
            for (size_t ri : removeIndices) {
                if (ri == mergeTarget) { targetRemoved = true; break; }
            }
            if (!targetRemoved) break;
            mergeTarget++;
        }
        if (debugApiMessages()) {
            std::cerr << "  merge_check: k=" << k << " mergeTarget=" << mergeTarget;
            if (mergeTarget < messages.size())
                std::cerr << " targetRole=" << roleLabel(messages[mergeTarget].role);
            std::cerr << " removeIndices=[";
            for (size_t ri : removeIndices) std::cerr << ri << " ";
            std::cerr << "]\n";
        }
        if (mergeTarget < messages.size()) {
            auto& targetMsg = messages[mergeTarget];
            if (targetMsg.role == MessageRole::User ||
                targetMsg.role == MessageRole::ToolResult) {
                // Prepend tool_results to the existing message.
                // Tool_results MUST come before text per Anthropic protocol.
                targetMsg.content.insert(targetMsg.content.begin(),
                                       consolidated.content.begin(),
                                       consolidated.content.end());
                if (targetMsg.role == MessageRole::ToolResult) {
                    targetMsg.role = MessageRole::User;
                }
                merged = true;
            }
        }

        if (!merged) {
            // Insert standalone consolidated message
            messages.insert(messages.begin() + i + 1, std::move(consolidated));

            // Remove emptied messages (+1 shift for the insertion)
            std::sort(removeIndices.begin(), removeIndices.end(), std::greater<size_t>());
            for (size_t rmIdx : removeIndices) {
                messages.erase(messages.begin() +
                               static_cast<ptrdiff_t>(rmIdx + 1));
            }
        } else {
            // Merge path: no insertion, no index shift
            std::sort(removeIndices.begin(), removeIndices.end(), std::greater<size_t>());
            for (size_t rmIdx : removeIndices) {
                messages.erase(messages.begin() +
                               static_cast<ptrdiff_t>(rmIdx));
            }
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
            out << "  " << (merged ? "merged_into_index" : "inserted_at_index")
                << ": " << (merged ? mergeTarget : i + 1) << "\n";
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

    // === Step 4: Coalesce adjacent same-role messages ===
    int coalesced = coalesceAdjacentSameRole(messages);

    dumpContentHistory("AFTER_REPAIR", messages);

    // === Step 5: Re-validate after coalesce ===
    // The coalesce should never create violations, but verify defensively.
    if (!validateEmptyMessages(messages) || !validateToolResultOrdering(messages)) {
        fprintf(stderr, "[ERROR] ContentBlockParam: protocol violation AFTER coalesce — "
                "rejecting history\n");
        // Return injected count so caller knows repair ran, but messages are
        // left in the repaired state for diagnostics.
    }

    return injected;
}

// ========== Coalesce adjacent same-role messages ==========

int coalesceAdjacentSameRole(std::vector<ContentMessage>& messages) {
    int coalesced = 0;
    // Diagnostic: log role sequence before coalesce
    if (debugApiMessages()) {
        std::cerr << "COALESCE_SCAN: roles=[";
        for (size_t si = 0; si < messages.size(); ++si) {
            if (si > 0) std::cerr << " ";
            std::cerr << (int)messages[si].role;
        }
        std::cerr << "] size=" << messages.size() << "\n";
    }
    for (size_t ci = 1; ci < messages.size(); ) {
        if (messages[ci].role == messages[ci - 1].role) {
            auto& prev = messages[ci - 1];
            auto& cur  = messages[ci];

            if (debugApiMessages()) {
                std::cerr << "COALESCE: merging [" << (ci-1) << "] "
                          << roleLabel(prev.role) << "(" << prev.content.size()
                          << " blocks) + [" << ci << "] "
                          << roleLabel(cur.role) << "(" << cur.content.size()
                          << " blocks)\n";
            }

            if (prev.role == MessageRole::User) {
                // User + User: prepend tool_results from 'cur' into 'prev'
                std::vector<ContentBlockParam> toolResults, other;
                for (auto& b : cur.content) {
                    if (b.type == ContentBlockParam::ToolResult)
                        toolResults.push_back(std::move(b));
                    else
                        other.push_back(std::move(b));
                }
                prev.content.insert(prev.content.begin(),
                    std::make_move_iterator(toolResults.begin()),
                    std::make_move_iterator(toolResults.end()));
                prev.content.insert(prev.content.end(),
                    std::make_move_iterator(other.begin()),
                    std::make_move_iterator(other.end()));
            } else {
                // Assistant + Assistant: append cur blocks to prev.
                // GUARD: never merge if either carries tool_use blocks.
                bool hasToolUse = false;
                for (auto& b : prev.content) {
                    if (b.type == ContentBlockParam::ToolUse) { hasToolUse = true; break; }
                }
                if (!hasToolUse) {
                    for (auto& b : cur.content) {
                        if (b.type == ContentBlockParam::ToolUse) { hasToolUse = true; break; }
                    }
                }
                if (hasToolUse) {
                    // Skip merge — tool-bearing assistants must not be coalesced.
                    // This indicates a structural issue in the compact pipeline;
                    // the caller should validate and reject before reaching here.
                    ci++;
                    continue;
                }
                prev.content.insert(prev.content.end(),
                    std::make_move_iterator(cur.content.begin()),
                    std::make_move_iterator(cur.content.end()));
            }

            messages.erase(messages.begin() + static_cast<ptrdiff_t>(ci));
            coalesced++;
            // Don't increment ci — re-check the new adjacent pair
        } else {
            ci++;
        }
    }
    if (coalesced > 0 && debugApiMessages()) {
        std::cerr << "COALESCE: " << coalesced << " adjacent pair(s) merged\n";
    }
    return coalesced;
}

// Convert ContentMessage back to legacy Message format.
// This is the reverse of convertLegacyMessage — used to persist repaired
// history back to impl_->messageHistory so repair is idempotent.
Message convertContentMessageToLegacy(const ContentMessage& cm) {
    Message m;
    m.role = cm.role;
    m.timestamp = cm.timestamp;
    m.apiRound = cm.apiRound;

    // Collect text from all Text blocks into msg.content
    String textAccum;
    for (auto& block : cm.content) {
        if (block.type == ContentBlockParam::Text && !block.text.empty()) {
            if (!textAccum.empty()) textAccum += " ";
            textAccum += block.text;
        } else if (block.type == ContentBlockParam::Thinking) {
            m.thinking = block.thinking;
            m.signature = block.signature;
        } else if (block.type == ContentBlockParam::ToolUse) {
            ToolCall tc;
            tc.id = block.id;
            tc.name = block.name;
            tc.arguments = block.input.dump();
            m.toolCalls.push_back(std::move(tc));
        } else if (block.type == ContentBlockParam::ToolResult) {
            ToolResponse tr;
            tr.callId = block.toolUseId;
            tr.content = block.resultContent;
            tr.isError = block.isError;
            m.toolResults.push_back(std::move(tr));
        } else if (block.type == ContentBlockParam::RedactedThinking) {
            m.redactedThinking.push_back({{"data", block.redactedData}});
        }
    }
    m.content = std::move(textAccum);

    return m;
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

bool validateSerializedApiJson(const Json& request) {
    if (!request.contains("messages")) {
        fprintf(stderr, "[ERROR] API JSON: missing 'messages' key\n");
        return false;
    }
    const auto& messages = request["messages"];
    if (!messages.is_array()) {
        fprintf(stderr, "[ERROR] API JSON: 'messages' is not an array\n");
        return false;
    }
    bool ok = true;
    String prevRole;

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        String role = msg.value("role", "");
        const auto& content = msg.value("content", Json::array());

        // Check 1: role must be "user" or "assistant"
        if (role != "user" && role != "assistant") {
            fprintf(stderr, "[ERROR] API JSON[%zu]: invalid role '%s'\n", i, role.c_str());
            ok = false;
        }

        // Check 2: content must be non-empty array
        if (!content.is_array() || content.empty()) {
            fprintf(stderr, "[ERROR] API JSON[%zu]: role=%s has EMPTY content array\n",
                    i, role.c_str());
            ok = false;
            prevRole = role;
            continue;
        }

        // Check 3: role alternation (no consecutive same role)
        if (!prevRole.empty() && role == prevRole) {
            fprintf(stderr, "[ERROR] API JSON[%zu]: consecutive same role '%s' (prev was [%zu])\n",
                    i, role.c_str(), i - 1);
            ok = false;
        }
        prevRole = role;

        // Check 4: each block must have valid type and non-empty content
        std::vector<String> toolUseIds, toolResultIds;
        for (size_t j = 0; j < content.size(); ++j) {
            const auto& block = content[j];
            String btype = block.value("type", "");

            if (btype == "text") {
                String text = block.value("text", "");
                if (text.empty()) {
                    fprintf(stderr, "[ERROR] API JSON[%zu].block[%zu]: EMPTY text\n", i, j);
                    ok = false;
                }
            } else if (btype == "tool_use") {
                String id = block.value("id", "");
                if (id.empty()) {
                    fprintf(stderr, "[ERROR] API JSON[%zu].block[%zu]: tool_use with EMPTY id\n", i, j);
                    ok = false;
                }
                toolUseIds.push_back(id);
            } else if (btype == "tool_result") {
                String tuid = block.value("tool_use_id", "");
                String c = block.value("content", "");
                if (tuid.empty()) {
                    fprintf(stderr, "[ERROR] API JSON[%zu].block[%zu]: tool_result with EMPTY tool_use_id\n", i, j);
                    ok = false;
                }
                if (c.empty()) {
                    fprintf(stderr, "[ERROR] API JSON[%zu].block[%zu]: tool_result with EMPTY content\n", i, j);
                    ok = false;
                }
                toolResultIds.push_back(tuid);
            } else if (btype == "thinking" || btype == "redacted_thinking") {
                // valid, skip
            } else {
                fprintf(stderr, "[ERROR] API JSON[%zu].block[%zu]: unknown type '%s'\n",
                        i, j, btype.c_str());
                ok = false;
            }
        }

        // Check 5: if previous msg was assistant with tool_use, current must have matching tool_results
        if (i > 0 && role == "user") {
            const auto& prev = messages[i - 1];
            if (prev.value("role", "") == "assistant") {
                // Collect tool_use IDs from previous assistant
                std::vector<String> prevToolUseIds;
                for (const auto& b : prev.value("content", Json::array())) {
                    if (b.value("type", "") == "tool_use") {
                        prevToolUseIds.push_back(b.value("id", ""));
                    }
                }
                if (!prevToolUseIds.empty()) {
                    // First blocks of current user message must be tool_results
                    if (toolResultIds.empty()) {
                        fprintf(stderr, "[ERROR] API JSON[%zu]: assistant[%zu] has %zu tool_use(s) "
                                "but user[%zu] has NO tool_results\n",
                                i, i - 1, prevToolUseIds.size(), i);
                        ok = false;
                    }
                }
            }
        }
    }

    // Check 6: no contentPreview or UI-only fields in JSON
    String body = request.dump();
    if (body.find("contentPreview") != String::npos) {
        fprintf(stderr, "[ERROR] API JSON: 'contentPreview' found in payload — UI field leak!\n");
        ok = false;
    }
    if (body.find("stableId") != String::npos) {
        fprintf(stderr, "[ERROR] API JSON: 'stableId' found in payload — UI field leak!\n");
        ok = false;
    }

    if (debugApiMessages()) {
        std::cerr << (ok ? "VALIDATION_SERIALIZED_JSON: PASS\n" : "VALIDATION_SERIALIZED_JSON: FAIL\n");
    }
    return ok;
}

bool validateEmptyMessages(const std::vector<ContentMessage>& messages) {
    bool ok = true;
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        // Check 1: message must have at least one content block
        if (msg.content.empty()) {
            fprintf(stderr, "[ERROR] Protocol: message[%zu] role=%s has EMPTY content\n",
                    i, roleLabel(msg.role));
            ok = false;
        }
        // Check 2: each text block must be non-empty
        for (size_t j = 0; j < msg.content.size(); ++j) {
            const auto& block = msg.content[j];
            if (block.type == ContentBlockParam::Text && block.text.empty()) {
                fprintf(stderr, "[ERROR] Protocol: message[%zu].block[%zu] is EMPTY text\n",
                        i, j);
                ok = false;
            }
        }
        // Check 3: assistant messages must not be empty
        if (msg.role == MessageRole::Assistant && msg.content.empty()) {
            fprintf(stderr, "[ERROR] Protocol: message[%zu] is EMPTY assistant — "
                    "likely a cancelled turn persisted without content\n", i);
            ok = false;
        }
    }
    if (debugApiMessages()) {
        std::cerr << (ok ? "VALIDATION_EMPTY: PASS\n" : "VALIDATION_EMPTY: FAIL\n");
    }
    return ok;
}

} // namespace claude
