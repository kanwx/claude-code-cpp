#pragma once

#include "UiMessageTypes.hpp"
#include <vector>
#include <memory>

namespace claude {

// ========== Stream Events ==========
// Raw input events from AgentLoop, mirroring TS stream events.
// These are the "source of truth" events that flow into the pipeline.

struct StreamEvent {
    enum class Type {
        TextDelta,          // Streaming text chunk
        ThinkingDelta,      // Thinking content chunk
        ToolUseStart,       // Tool invocation begins (tool_use block started)
        ToolUseComplete,    // Tool invocation input complete (tool_use block stopped)
        ToolResultReady,    // Tool execution finished, result available
        StreamStart,        // New API request begins
        StreamEnd,          // API response complete
        StreamError,        // Error during streaming
        UserMessage,        // User submitted input
        SystemMessage,      // System informational message
        ErrorMessage,       // Error message
        TurnDuration,       // Turn duration summary
        CompactBoundary,    // Context compaction marker
        HookSummary,        // Hook progress summary
        Tombstone,          // Model fallback — previous partial content invalidated
    };

    Type type;

    // Content (which fields are valid depends on type)
    String text;               // TextDelta, ThinkingDelta, UserMessage, SystemMessage, ErrorMessage, TurnDuration
    String toolId;             // ToolUseStart, ToolUseComplete, ToolResultReady
    String toolName;           // ToolUseStart, ToolUseComplete, ToolResultReady
    String toolInput;          // ToolUseStart (partial), ToolUseComplete (full JSON)
    String toolResult;         // ToolResultReady
    bool toolIsError = false;  // ToolResultReady
    bool toolIsCancelled = false; // ToolResultReady
    bool toolIsRejected = false;  // ToolResultReady
    bool success = true;       // StreamEnd
    String fallbackFromModel;   // For Tombstone events
    String fallbackToModel;     // For Tombstone events
};

// ========== Pipeline Stage Interface ==========

class PipelineStage {
public:
    virtual ~PipelineStage() = default;

    /// Process an incoming stream event and update the message list.
    /// Returns true if messages were modified (triggering a re-render).
    virtual bool processEvent(const StreamEvent& event,
                              std::vector<DisplayMessage>& messages) = 0;

    /// Get the stage name (for debugging)
    virtual const char* name() const = 0;
};

// ========== Normalize Stage ==========
// Converts raw events into properly structured DisplayMessages.
// Handles: streaming→final text transition, tool_use/tool_result pairing.

class NormalizeStage : public PipelineStage {
public:
    bool processEvent(const StreamEvent& event,
                      std::vector<DisplayMessage>& messages) override;
    const char* name() const override { return "Normalize"; }

    // Access the current streaming state
    bool isStreaming() const { return isStreaming_; }
    const String& streamingText() const { return streamingText_; }
    const String& thinkingSummary() const { return thinkingSummary_; }
    bool isThinking() const { return isThinking_; }

    void setStreamingText(const String& text) { streamingText_ = text; }
    void setThinking(bool v) { isThinking_ = v; }
    void setStreaming(bool v) { isStreaming_ = v; }

private:
    bool isStreaming_ = false;
    bool isThinking_ = false;
    String streamingText_;
    String thinkingSummary_;

    // Pending tool_use blocks waiting for results (keyed by toolId)
    std::unordered_map<String, size_t> pendingToolUseIndex_;
};

// ========== Group Stage ==========
// Groups consecutive read/search tool_use+tool_result pairs into
// CollapsedReadSearch messages. Mirrors TS applyGrouping().

class GroupStage : public PipelineStage {
public:
    bool processEvent(const StreamEvent& event,
                      std::vector<DisplayMessage>& messages) override;
    const char* name() const override { return "Group"; }

    /// Force re-grouping of all messages (e.g. after a batch of changes)
    void regroupAll(std::vector<DisplayMessage>& messages);

    bool verboseMode() const { return verboseMode_; }
    void setVerboseMode(bool v) { verboseMode_ = v; }

private:
    bool verboseMode_ = false;
    bool needsRegroup_ = false;

    /// Scan messages and collapse consecutive read/search tools
    void applyGrouping(std::vector<DisplayMessage>& messages);
};

// ========== Collapse Stage ==========
// Collapses HookSummary messages and background task notifications
// that would otherwise create visual noise.

class CollapseStage : public PipelineStage {
public:
    bool processEvent(const StreamEvent& event,
                      std::vector<DisplayMessage>& messages) override;
    const char* name() const override { return "Collapse"; }

    /// Merge consecutive SystemInfo messages that are duplicates
    void deduplicateSystemMessages(std::vector<DisplayMessage>& messages);
};

// ========== Message Pipeline ==========
// Composable pipeline that processes StreamEvents through stages
// and produces a final DisplayMessage list for rendering.

class MessagePipeline {
public:
    MessagePipeline();

    /// Process an incoming event through all pipeline stages.
    /// Returns true if the display message list was modified.
    bool processEvent(const StreamEvent& event);

    /// Force a full re-processing (e.g. after mode change)
    void reprocess();

    /// Get the final display messages for rendering.
    /// This is the pipeline output after all stages.
    const std::vector<DisplayMessage>& getDisplayMessages() const { return displayMessages_; }

    /// Get the raw (pre-pipeline) message list.
    /// Used for indexing into original messages (e.g. tool grouping references).
    std::vector<DisplayMessage>& rawMessages() { return rawMessages_; }

    /// Access individual stages for configuration
    NormalizeStage& normalizeStage() { return *normalizeStage_; }
    GroupStage& groupStage() { return *groupStage_; }
    CollapseStage& collapseStage() { return *collapseStage_; }

    // Streaming state accessors (delegated to NormalizeStage)
    bool isStreaming() const { return normalizeStage_->isStreaming(); }
    bool isThinking() const { return normalizeStage_->isThinking(); }
    const String& streamingText() const { return normalizeStage_->streamingText(); }
    const String& thinkingSummary() const { return normalizeStage_->thinkingSummary(); }

private:
    // Stages in order
    std::unique_ptr<NormalizeStage> normalizeStage_;
    std::unique_ptr<GroupStage> groupStage_;
    std::unique_ptr<CollapseStage> collapseStage_;

    // Raw messages (before grouping/collapsing)
    std::vector<DisplayMessage> rawMessages_;

    // Final display messages (after all stages)
    std::vector<DisplayMessage> displayMessages_;
};

} // namespace claude
