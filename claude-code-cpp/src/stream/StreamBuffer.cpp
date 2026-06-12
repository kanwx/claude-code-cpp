#include "claude/stream/StreamBuffer.hpp"

namespace claude {

// Strip thinking tags from a string. Handles:
//   <thinking>...</thinking>       (standard Anthropic format)
//   <think>...</think>             (DeepSeek format)
//   ＜thinking＞...＜/thinking＞       (CJK fullwidth angle bracket variants)
//   ＜think＞...＜/think＞             (CJK fullwidth angle bracket variants)
static String stripThinkingTags(const String& text) {
    String result = text;
    // All open/close tag pairs to try
    static const std::vector<std::pair<String, String>> tagPairs = {
        {"<thinking>", "</thinking>"},
        {"<think>", "</think>"},
        {"\xef\xbc\x9c" "thinking" "\xef\xbc\x9e", "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e"},
        {"\xef\xbc\x9c" "think" "\xef\xbc\x9e", "\xef\xbc\x9c" "/think" "\xef\xbc\x9e"},
        {"&lt;thinking&gt;", "&lt;/thinking&gt;"},
        {"&lt;think&gt;", "&lt;/think&gt;"},
    };

    for (const auto& [openTag, closeTag] : tagPairs) {
        size_t pos;
        while ((pos = result.find(openTag)) != String::npos) {
            auto endPos = result.find(closeTag, pos + openTag.size());
            if (endPos != String::npos) {
                result.erase(pos, endPos + closeTag.size() - pos);
            } else {
                result.erase(pos);
                break;
            }
        }
    }
    return result;
}

void StreamBuffer::feed(TypedStreamEvent&& event) {
    std::lock_guard lock(mutex_);
    switch (event.type) {
        case StreamEventType::StreamStart:
            answerStarted_ = true;
            inThinkingTag_ = false;
            thinkingTagBuffer_.clear();      // Clear stale partial prefixes from previous stream
            textAccumulator_.clear();
            lastEmittedPos_ = 0;
            blockParser_.reset();
            answerStartTime_ = std::chrono::steady_clock::now();
            emit(DisplayEvent{.type = DisplayEventType::AnswerStart});
            emit(DisplayEvent{.type = DisplayEventType::TurnMetadata, .metadata = {.isStreaming = true}});
            break;

        case StreamEventType::TextDelta: {
            // Handle thinking tags in text output.
            // Models like DeepSeek embed reasoning in <think>...</think> tags
            // within the text stream. Instead of DISCARDING this content, we
            // route it to thinkingAccumulator_ so it appears as expandable
            // "∴ Thinking" in the UI (matching Anthropic's native thinking blocks).
            String cleanText = event.text;

            // B3: Prepend any buffered partial prefix from previous chunk
            if (!thinkingTagBuffer_.empty()) {
                cleanText = thinkingTagBuffer_ + cleanText;
                thinkingTagBuffer_.clear();
            }

            // Cross-turn close tag detection: when a tool call interrupts
            // thinking, the close tag may arrive in a new turn after
            // StreamStart resets inThinkingTag_. Route orphaned content to thinking.
            if (!inThinkingTag_) {
                size_t closePos = String::npos;
                size_t closeLen = 0;
                for (const char* ct : {"</thinking>", "</think>",
                                       "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e",
                                       "\xef\xbc\x9c" "/think" "\xef\xbc\x9e",
                                       "&lt;/thinking&gt;", "&lt;/think&gt;"}) {
                    auto p = cleanText.find(ct);
                    if (p != String::npos && (closePos == String::npos || p < closePos)) {
                        closePos = p;
                        closeLen = strlen(ct);
                    }
                }
                if (closePos != String::npos) {
                    // Dangling close tag: route orphaned content to thinking
                    String orphan = cleanText.substr(0, closePos);
                    if (!orphan.empty() && orphan.find_first_not_of(" \t\n\r") != String::npos) {
                        thinkingAccumulator_ += orphan;
                        thinkingCharsSinceEmit_ += orphan.size();
                        maybeEmitThinkingUpdate(false);
                    }
                    cleanText = cleanText.substr(closePos + closeLen);
                }
            }
            if (inThinkingTag_) {
                size_t endPos = String::npos;
                size_t closeLen = 0;
                for (const char* ct : {"</thinking>", "</think>",
                                       "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e",
                                       "\xef\xbc\x9c" "/think" "\xef\xbc\x9e",
                                       "&lt;/thinking&gt;", "&lt;/think&gt;"}) {
                    auto p = cleanText.find(ct);
                    if (p != String::npos && (endPos == String::npos || p < endPos)) {
                        endPos = p;
                        closeLen = strlen(ct);
                    }
                }
                if (endPos != String::npos) {
                    // Extract thinking content before the close tag
                    String thinkContent = cleanText.substr(0, endPos);
                    if (!thinkContent.empty()) {
                        thinkingAccumulator_ += thinkContent;
                        thinkingCharsSinceEmit_ += thinkContent.size();
                        maybeEmitThinkingUpdate(false);
                    }
                    inThinkingTag_ = false;
                    cleanText = cleanText.substr(endPos + closeLen);
                } else {
                    // Entire remaining chunk is thinking content
                    if (!cleanText.empty() && cleanText.find_first_not_of(" \t\n\r") != String::npos) {
                        thinkingAccumulator_ += cleanText;
                        thinkingCharsSinceEmit_ += cleanText.size();
                        maybeEmitThinkingUpdate(false);
                    }
                    break;
                }
            }
            // Check all open tag variants — extract content, route to thinking
            static const std::vector<std::pair<String, String>> tagPairs = {
                {"<thinking>", "</thinking>"},
                {"<think>", "</think>"},
                {"\xef\xbc\x9c" "thinking" "\xef\xbc\x9e", "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e"},
                {"\xef\xbc\x9c" "think" "\xef\xbc\x9e", "\xef\xbc\x9c" "/think" "\xef\xbc\x9e"},
                {"&lt;thinking&gt;", "&lt;/thinking&gt;"},
                {"&lt;think&gt;", "&lt;/think&gt;"},
            };
            for (const auto& [openTag, closeTag] : tagPairs) {
                size_t pos;
                while ((pos = cleanText.find(openTag)) != String::npos) {
                    auto endPos = cleanText.find(closeTag, pos + openTag.size());
                    if (endPos != String::npos) {
                        // Extract thinking content between tags, route to thinking accumulator
                        String thinkContent = cleanText.substr(pos + openTag.size(),
                                                               endPos - pos - openTag.size());
                        if (!thinkContent.empty()) {
                            thinkingAccumulator_ += thinkContent;
                            thinkingCharsSinceEmit_ += thinkContent.size();
                            maybeEmitThinkingUpdate(false);
                        }
                        cleanText.erase(pos, endPos + closeTag.size() - pos);
                    } else {
                        // Open tag without close tag: content continues in next chunk.
                        // Extract text after open tag, route to thinking accumulator.
                        String thinkContent = cleanText.substr(pos + openTag.size());
                        if (!thinkContent.empty()) {
                            thinkingAccumulator_ += thinkContent;
                            thinkingCharsSinceEmit_ += thinkContent.size();
                            maybeEmitThinkingUpdate(false);
                        }
                        cleanText.erase(pos);
                        inThinkingTag_ = true;
                        break;
                    }
                }
            }

            // Enhanced partial tag prefix detection using buffer (Fix B3).
            // When a chunk ends with a partial prefix like "<thin", buffer it
            // for the next chunk instead of discarding it.
            if (!cleanText.empty()) {
                auto ltPos = cleanText.rfind('<');
                if (ltPos != String::npos) {
                    String sfx = cleanText.substr(ltPos);
                    // Check if sfx is a prefix of any known tag (open or close)
                    bool isPartialPrefix = false;
                    for (const auto& [openTag, closeTag] : tagPairs) {
                        if (openTag.size() > sfx.size() && openTag.compare(0, sfx.size(), sfx) == 0) {
                            isPartialPrefix = true; break;
                        }
                        if (closeTag.size() > sfx.size() && closeTag.compare(0, sfx.size(), sfx) == 0) {
                            isPartialPrefix = true; break;
                        }
                    }
                    // Also check &lt; variants (they start with '&', not '<')
                    if (!isPartialPrefix) {
                        for (const char* p : {"&lt;think", "&lt;thinking", "&lt;/think", "&lt;/thinking"}) {
                            String ps(p);
                            if (ps.size() > sfx.size() && ps.compare(0, sfx.size(), sfx) == 0) {
                                isPartialPrefix = true; break;
                            }
                        }
                    }
                    if (isPartialPrefix) {
                        thinkingTagBuffer_ = sfx;  // Buffer for next chunk
                        cleanText.erase(ltPos);
                    }
                }

                // Also check for fullwidth ＜ prefix (U+FF1C = 0xEF 0xBC 0x9C)
                auto fwPos = cleanText.rfind('\xef');
                if (fwPos != String::npos && fwPos > ltPos) {
                    String fwSfx = cleanText.substr(fwPos);
                    String fwThink = "\xef\xbc\x9c" "think" "\xef\xbc\x9e";
                    String fwThinking = "\xef\xbc\x9c" "thinking" "\xef\xbc\x9e";
                    String fwCloseThink = "\xef\xbc\x9c" "/think" "\xef\xbc\x9e";
                    String fwCloseThinking = "\xef\xbc\x9c" "/thinking" "\xef\xbc\x9e";
                    if (fwThink.size() > fwSfx.size() && fwThink.compare(0, fwSfx.size(), fwSfx) == 0) {
                        thinkingTagBuffer_ = fwSfx;
                        cleanText.erase(fwPos);
                    } else if (fwThinking.size() > fwSfx.size() && fwThinking.compare(0, fwSfx.size(), fwSfx) == 0) {
                        thinkingTagBuffer_ = fwSfx;
                        cleanText.erase(fwPos);
                    } else if (fwCloseThink.size() > fwSfx.size() && fwCloseThink.compare(0, fwSfx.size(), fwSfx) == 0) {
                        thinkingTagBuffer_ = fwSfx;
                        cleanText.erase(fwPos);
                    } else if (fwCloseThinking.size() > fwSfx.size() && fwCloseThinking.compare(0, fwSfx.size(), fwSfx) == 0) {
                        thinkingTagBuffer_ = fwSfx;
                        cleanText.erase(fwPos);
                    }
                }
            }

            if (cleanText.empty()) break;

            textAccumulator_ += cleanText;
            bool hasBoundary = blockParser_.append(cleanText);

            if (hasBoundary && blockParser_.lastBoundaryPos() > 0) {
                // Paragraph boundary detected: flush the complete paragraph.
                lastEmittedPos_ = 0;
                flushTextBuffer(false);
            } else if (textAccumulator_.size() >= FLUSH_THRESHOLD) {
                // Safety-net: long paragraph with no block boundary detected.
                // Emit ONLY the incremental delta since the last emit.
                // The UI (FtxuiRepl) APPENDS TextPartial to streamingText_,
                // so we must not re-send already-emitted text.
                String delta = textAccumulator_.substr(lastEmittedPos_);
                lastEmittedPos_ = textAccumulator_.size();
                if (!delta.empty()) {
                    emit(DisplayEvent{
                        .type = DisplayEventType::TextPartial,
                        .text = std::move(delta)
                    });
                }
                // Do NOT clear textAccumulator_ — the full paragraph is sent
                // as TextParagraph at the next boundary or StreamEnd.
            }
            break;
        }

        case StreamEventType::ThinkingDelta: {
            // Strip thinking tags from ThinkingDelta too.
            // Some models leak raw tags into thinking events.
            String clean = stripThinkingTags(event.text);
            if (!clean.empty()) {
                thinkingAccumulator_ += clean;
                thinkingCharsSinceEmit_ += clean.size();
            }
            maybeEmitThinkingUpdate(false);
            break;
        }

        case StreamEventType::ThinkingBlockStop:
            maybeEmitThinkingUpdate(true);
            break;

        case StreamEventType::ToolUseStart:
            break;

        case StreamEventType::ToolUseComplete: {
            PendingToolCall ptc;
            ptc.callId = event.toolCall.id;
            ptc.toolName = event.toolCall.name;
            ptc.state = StreamToolEventType::Queued;
            pendingTools_[event.toolCall.id] = std::move(ptc);
            break;
        }

        case StreamEventType::UsageUpdate: {
            DisplayEvent meta{.type = DisplayEventType::TurnMetadata};
            meta.metadata.isStreaming = true;
            meta.metadata.inputTokens = event.usage.promptTokens;
            meta.metadata.outputTokens = event.usage.completionTokens;
            emit(std::move(meta));
            break;
        }

        case StreamEventType::StreamEnd: {
            flushTextBuffer(true);
            maybeEmitThinkingUpdate(true);
            auto elapsed = std::chrono::steady_clock::now() - answerStartTime_;
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
            String durationStr;
            if (secs.count() >= 60) {
                durationStr = std::to_string(secs.count() / 60) + "m " + std::to_string(secs.count() % 60) + "s";
            } else {
                durationStr = std::to_string(secs.count()) + "s";
            }
            emit(DisplayEvent{.type = DisplayEventType::AnswerEnd});
            emit(DisplayEvent{.type = DisplayEventType::TurnMetadata, .metadata = {
                .durationStr = durationStr,
                .isStreaming = false
            }});
            break;
        }

        case StreamEventType::Error:
            emit(DisplayEvent{.type = DisplayEventType::Error, .text = std::move(event.error)});
            break;

        default:
            break;
    }
}

void StreamBuffer::feed(StreamToolEvent&& event) {
    std::lock_guard lock(mutex_);
    switch (event.type) {
        case StreamToolEventType::Started: {
            auto it = pendingTools_.find(event.toolCallId);
            if (it != pendingTools_.end()) {
                it->second.state = StreamToolEventType::Started;
                it->second.activity = event.activity;
            }
            flushTextBuffer(false);
            emit(DisplayEvent{
                .type = DisplayEventType::ToolProgress,
                .toolCallId = event.toolCallId,
                .toolName = event.toolName,
                .activity = std::move(event.activity)
            });
            break;
        }

        case StreamToolEventType::Completed: {
            auto it = pendingTools_.find(event.toolCallId);
            if (it != pendingTools_.end()) {
                it->second.state = StreamToolEventType::Completed;
            }
            flushTextBuffer(false);
            emit(DisplayEvent{
                .type = DisplayEventType::ToolResult,
                .toolCallId = event.toolCallId,
                .toolName = event.toolName,
                .summary = std::move(event.summary),
                .rawResultPath = std::move(event.rawResultPath)
            });
            break;
        }

        case StreamToolEventType::Error: {
            flushTextBuffer(false);
            emit(DisplayEvent{
                .type = DisplayEventType::ToolResult,
                .toolCallId = event.toolCallId,
                .toolName = event.toolName,
                .summary = ToolResultSummary::error(event.toolName + " failed")
            });
            break;
        }

        case StreamToolEventType::Rejected: {
            flushTextBuffer(false);
            emit(DisplayEvent{
                .type = DisplayEventType::ToolResult,
                .toolCallId = event.toolCallId,
                .toolName = event.toolName,
                .summary = ToolResultSummary::dim("Rejected")
            });
            break;
        }

        case StreamToolEventType::Cancelled: {
            flushTextBuffer(false);
            emit(DisplayEvent{
                .type = DisplayEventType::ToolResult,
                .toolCallId = event.toolCallId,
                .toolName = event.toolName,
                .summary = ToolResultSummary::dim("Interrupted")
            });
            break;
        }

        default:
            break;
    }
}

void StreamBuffer::setDisplayCallback(std::function<void(DisplayEvent&&)> cb) {
    onDisplay_ = std::move(cb);
}

void StreamBuffer::flush() {
    std::lock_guard lock(mutex_);
    flushTextBuffer(true);
    maybeEmitThinkingUpdate(true);
}

void StreamBuffer::flushTextBuffer(bool isComplete) {
    if (textAccumulator_.empty()) return;
    // TextPartial already emitted per-token above. On paragraph boundary,
    // emit TextParagraph to signal the UI that the paragraph is complete
    // (so it can finalize the streaming block into a committed message).
    emit(DisplayEvent{
        .type = DisplayEventType::TextParagraph,
        .text = std::move(textAccumulator_)
    });
    textAccumulator_.clear();
    lastEmittedPos_ = 0;     // Reset delta tracker (Fix B1)
    blockParser_.reset();
}

void StreamBuffer::maybeEmitThinkingUpdate(bool force) {
    if (thinkingAccumulator_.empty()) return;

    auto now = std::chrono::steady_clock::now();
    bool intervalOk = (now - lastThinkingEmit_) >= THINKING_MIN_INTERVAL;
    bool charsOk = thinkingCharsSinceEmit_ >= THINKING_MIN_CHARS;

    if (force || (intervalOk && charsOk)) {
        // Strip any residual thinking tags before emitting.
        String clean = stripThinkingTags(thinkingAccumulator_);
        if (!clean.empty()) {
            emit(DisplayEvent{
                .type = DisplayEventType::ThinkingBlock,
                .thinkingText = std::move(clean)
            });
        }
        lastThinkingEmit_ = now;
        thinkingCharsSinceEmit_ = 0;
    }
}

void StreamBuffer::emit(DisplayEvent&& event) {
    if (onDisplay_) {
        onDisplay_(std::move(event));
    }
}

} // namespace claude
