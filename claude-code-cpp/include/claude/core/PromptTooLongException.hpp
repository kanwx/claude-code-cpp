#pragma once
#include <stdexcept>
#include "claude/core/Types.hpp"

namespace claude {

struct PromptTooLongException : public std::runtime_error {
    int actualTokens = 0;
    int maxTokens = 0;

    explicit PromptTooLongException(const String& msg)
        : std::runtime_error(msg) {}

    PromptTooLongException(const String& msg, int actual, int max)
        : std::runtime_error(msg), actualTokens(actual), maxTokens(max) {}
};

} // namespace claude
