#pragma once

#include <claude/core/Types.hpp>
#include <algorithm>
#include <vector>

namespace claude {
namespace api {

/// Anthropic API beta header constants and builder utilities.
struct BetaHeaders {
    // Beta header string constants
    static constexpr const char* PROMPT_CACHING      = "prompt-caching-2024-07-31";
    static constexpr const char* MAX_TOKENS_3_5      = "max-tokens-3-5-2024-07-31";
    static constexpr const char* REDACTED_THINKING   = "redacted-thinking-2025-04-01";
    static constexpr const char* CONTEXT_MANAGEMENT  = "context-management-2025-04-01";
    static constexpr const char* FAST_MODE           = "interleaved-thinking-2025-05-14";
    static constexpr const char* STRUCTURED_OUTPUTS  = "structured-outputs-2025-04-01";
    static constexpr const char* TOKEN_COUNTING      = "token-counting-2024-11-01";
    static constexpr const char* COMPUTER_USE        = "computer-use-2025-01-24";
    static constexpr const char* PDFS               = "pdfs-2024-09-25";
    static constexpr const char* CODE_EXECUTION      = "code-execution-2025-05-22";

    /// Default beta headers (prompt caching, max-tokens-3-5, token counting).
    static std::vector<String> getDefault() {
        return {PROMPT_CACHING, MAX_TOKENS_3_5, TOKEN_COUNTING};
    }

    /// Default + redacted thinking (for extended thinking mode).
    static std::vector<String> forExtendedThinking() {
        auto betas = getDefault();
        betas.push_back(REDACTED_THINKING);
        return betas;
    }

    /// Default + context management.
    static std::vector<String> forContextManagement() {
        auto betas = getDefault();
        betas.push_back(CONTEXT_MANAGEMENT);
        return betas;
    }

    /// Default + fast mode (interleaved thinking).
    static std::vector<String> forFastMode() {
        auto betas = getDefault();
        betas.push_back(FAST_MODE);
        return betas;
    }

    /// Build comma-separated header value from a list of beta strings.
    /// Returns empty string if the list is empty.
    static String buildHeaderString(const std::vector<String>& betas) {
        if (betas.empty()) return {};
        String result;
        for (size_t i = 0; i < betas.size(); ++i) {
            if (i > 0) result += ',';
            result += betas[i];
        }
        return result;
    }

    /// Merge two beta lists with deduplication, preserving order.
    static std::vector<String> merge(const std::vector<String>& a,
                                     const std::vector<String>& b) {
        std::vector<String> result = a;
        for (const auto& s : b) {
            if (std::find(result.begin(), result.end(), s) == result.end()) {
                result.push_back(s);
            }
        }
        return result;
    }
};

} // namespace api
} // namespace claude
