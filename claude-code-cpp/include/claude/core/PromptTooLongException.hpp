#pragma once
#include <stdexcept>
#include "claude/core/Types.hpp"

namespace claude {

/// Thrown when the API returns 413 prompt-too-long or overloaded_error.
/// Carries structured token gap data instead of requiring string parsing.
class PromptTooLongException : public std::runtime_error {
public:
    /// Construct with a custom message (e.g. from SSE error event).
    explicit PromptTooLongException(const String& msg)
        : std::runtime_error(msg) {}

    /// Construct with actual/max token counts (auto-generates message).
    PromptTooLongException(long actual, long max)
        : std::runtime_error("Prompt too long: " + std::to_string(actual) +
                             " tokens > " + std::to_string(max) + " limit")
        , actualTokens_(actual), maxTokens_(max) {}

    /// Construct with custom message and actual/max token counts.
    PromptTooLongException(const String& msg, long actual, long max)
        : std::runtime_error(msg), actualTokens_(actual), maxTokens_(max) {}

    /// Accessor: actual token count used.
    long actualTokens() const { return actualTokens_; }

    /// Accessor: maximum allowed token count.
    long maxTokens() const { return maxTokens_; }

    /// Accessor: how many tokens over the limit.
    long tokenGap() const { return actualTokens_ - maxTokens_; }

private:
    long actualTokens_ = 0;
    long maxTokens_ = 0;
};

} // namespace claude
