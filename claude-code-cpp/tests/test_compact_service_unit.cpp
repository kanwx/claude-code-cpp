#include <catch2/catch_test_macros.hpp>
#include <claude/core/ApiTypes.hpp>
#include <claude/core/compact/CompactService.hpp>
#include <chrono>

using namespace claude;
using namespace claude::compact;

TEST_CASE("Usage effectiveInputTokens calculation", "[compact][usage]") {
    Usage usage;
    usage.promptTokens = 150000;
    usage.cacheReadTokens = 50000;

    CHECK(usage.effectiveInputTokens() == 100000);
}

TEST_CASE("Message timestamp age filtering", "[compact][message]") {
    // Create a recent message (default timestamp = now)
    auto now = std::chrono::steady_clock::now();
    Message recentMsg = Message::user("recent message");

    // Create an old message (timestamp 10 minutes ago)
    auto tenMinAgo = now - std::chrono::minutes(10);
    Message oldMsg = Message::user("old message");
    oldMsg.timestamp = tenMinAgo;

    // Count messages older than 5 minutes
    auto threshold = now - std::chrono::minutes(5);
    int oldCount = 0;
    std::vector<Message> messages = {oldMsg, recentMsg};
    for (const auto& msg : messages) {
        if (msg.timestamp < threshold) {
            ++oldCount;
        }
    }
    CHECK(oldCount == 1);
}

TEST_CASE("CompactConfig defaults", "[compact][config]") {
    CompactConfig config;

    CHECK(config.maxOutputTokens == 8000);
    CHECK(config.retainRecentMsgs == 4);
    CHECK(config.stripImages == true);
}

TEST_CASE("CompactService estimateTokens", "[compact][service]") {
    CompactService service;

    SECTION("non-empty string returns positive token count") {
        size_t tokens = service.estimateTokens("Hello, this is a test message with some content.");
        CHECK(tokens > 0);
    }

    SECTION("empty string returns zero") {
        size_t tokens = service.estimateTokens("");
        CHECK(tokens == 0);
    }
}

TEST_CASE("CompactService buildCompactPrompt format", "[compact][service]") {
    CompactService service;

    std::vector<Message> messages;
    messages.push_back(Message::user("What is the capital of France?"));
    messages.push_back(Message::assistant("The capital of France is Paris."));

    String prompt = service.buildCompactPrompt(messages);

    CHECK(prompt.find("What is the capital of France?") != String::npos);
}
