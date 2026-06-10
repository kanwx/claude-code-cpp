#include "claude/stream/StreamBuffer.hpp"

namespace claude {

void StreamBuffer::feed(TypedStreamEvent&& event) {
    std::lock_guard lock(mutex_);
    switch (event.type) {
        case StreamEventType::StreamStart:
            answerStarted_ = true;
            answerStartTime_ = std::chrono::steady_clock::now();
            emit(DisplayEvent{.type = DisplayEventType::AnswerStart});
            emit(DisplayEvent{.type = DisplayEventType::TurnMetadata, .metadata = {.isStreaming = true}});
            break;

        case StreamEventType::TextDelta: {
            // Strip thinking tags from text output.
            // Some models (GLM, etc.) include <thinking>/<think> tags in regular
            // text instead of using separate thinking events. We must remove these.
            String cleanText = event.text;
            if (inThinkingTag_) {
                // Currently inside a thinking block — discard until close tag
                auto endPos = cleanText.find("</thinking>");
                if (endPos == String::npos) endPos = cleanText.find("</think>");
                if (endPos != String::npos) {
                    inThinkingTag_ = false;
                    cleanText = cleanText.substr(endPos + (cleanText[endPos+2] == 't' ? 12 : 8));
                } else {
                    break;  // Still inside thinking, discard entire chunk
                }
            }
            // Check for opening thinking tags
            for (const auto& tag : {"<thinking>", "<think>"}) {
                size_t tagLen = strlen(tag);
                size_t pos;
                while ((pos = cleanText.find(tag)) != String::npos) {
                    const char* closeTag = (tagLen == 11) ? "</thinking>" : "</think>";
                    size_t closeLen = (tagLen == 11) ? 12 : 8;
                    auto endPos = cleanText.find(closeTag, pos + tagLen);
                    if (endPos != String::npos) {
                        cleanText.erase(pos, endPos + closeLen - pos);
                    } else {
                        // Unclosed tag — discard from opening tag onward
                        cleanText.erase(pos);
                        inThinkingTag_ = true;
                        break;
                    }
                }
            }
            if (cleanText.empty()) break;

            textAccumulator_ += cleanText;
            emit(DisplayEvent{.type = DisplayEventType::TextPartial, .text = cleanText});
            if (blockParser_.append(cleanText)) {
                flushTextBuffer(false);
            }
            break;
        }

        case StreamEventType::ThinkingDelta:
            thinkingAccumulator_ += event.text;
            thinkingCharsSinceEmit_ += event.text.size();
            maybeEmitThinkingUpdate(false);
            break;

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
    blockParser_.reset();
}

void StreamBuffer::maybeEmitThinkingUpdate(bool force) {
    if (thinkingAccumulator_.empty()) return;

    auto now = std::chrono::steady_clock::now();
    bool intervalOk = (now - lastThinkingEmit_) >= THINKING_MIN_INTERVAL;
    bool charsOk = thinkingCharsSinceEmit_ >= THINKING_MIN_CHARS;

    if (force || (intervalOk && charsOk)) {
        emit(DisplayEvent{
            .type = DisplayEventType::ThinkingBlock,
            .thinkingText = thinkingAccumulator_
        });
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
