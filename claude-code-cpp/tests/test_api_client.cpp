#include <catch2/catch_test_macros.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/OpenAIClient.hpp>
#include <claude/api/BedrockClient.hpp>
#include <claude/core/Types.hpp>

using namespace claude;

TEST_CASE("AnthropicClient configuration", "[api]") {
    AnthropicClient client;

    SECTION("Default configuration") {
        REQUIRE(client.getProviderName() == "anthropic");
        REQUIRE_FALSE(client.getModelName().empty());
    }

    SECTION("Set API key") {
        client.setApiKey("test-key-12345");
        // Key is stored internally, no getter for security
    }

    SECTION("Set model") {
        client.setModel("claude-opus-4-20250514");
        REQUIRE(client.getModelName() == "claude-opus-4-20250514");
    }

    SECTION("Set max tokens") {
        client.setMaxTokens(4096);
        // Stored internally
    }

    SECTION("Custom base URL") {
        client.setBaseUrl("https://custom.api.com/v1");
        // Stored internally
    }
}

TEST_CASE("AnthropicClient with API key", "[api]") {
    SECTION("Constructor with key") {
        AnthropicClient client("sk-test-12345");
        REQUIRE(client.getProviderName() == "anthropic");
    }
}

TEST_CASE("OpenAIClient configuration", "[api]") {
    OpenAIClient client;

    SECTION("Default configuration") {
        REQUIRE(client.getProviderName() == "openai");
        REQUIRE_FALSE(client.getModelName().empty());
    }

    SECTION("Set API key") {
        client.setApiKey("sk-openai-test");
    }

    SECTION("Set model") {
        client.setModel("gpt-4o");
        REQUIRE(client.getModelName() == "gpt-4o");
    }
}

TEST_CASE("OpenAIClient with API key", "[api]") {
    SECTION("Constructor with key") {
        OpenAIClient client("sk-openai-12345");
        REQUIRE(client.getProviderName() == "openai");
    }
}

TEST_CASE("Message construction", "[api]") {
    SECTION("User message") {
        Message msg = Message::user("Hello, world!");
        REQUIRE(msg.role == MessageRole::User);
        REQUIRE(msg.content == "Hello, world!");
        REQUIRE_FALSE(msg.hasToolCalls());
    }

    SECTION("Assistant message") {
        Message msg = Message::assistant("Hi there!");
        REQUIRE(msg.role == MessageRole::Assistant);
        REQUIRE(msg.content == "Hi there!");
    }

    SECTION("System message") {
        Message msg = Message::system("You are a helpful assistant.");
        REQUIRE(msg.role == MessageRole::System);
        REQUIRE(msg.content == "You are a helpful assistant.");
    }

    SECTION("Tool result message") {
        std::vector<ToolResponse> results;
        results.push_back({"call-123", "Read", "file contents", false});
        Message msg = Message::toolResult(std::move(results));
        REQUIRE(msg.role == MessageRole::ToolResult);
        REQUIRE(msg.toolResults.size() == 1);
    }
}

TEST_CASE("Message with tool calls", "[api]") {
    std::vector<ToolCall> calls;
    calls.push_back({"call-1", "Read", R"({"file_path": "/test.txt"})"});
    calls.push_back({"call-2", "Bash", R"({"command": "ls"})"});

    Message msg = Message::assistant("Let me help you.", std::move(calls));

    REQUIRE(msg.role == MessageRole::Assistant);
    REQUIRE(msg.content == "Let me help you.");
    REQUIRE(msg.hasToolCalls());
    REQUIRE(msg.toolCalls.size() == 2);
}

TEST_CASE("Usage tracking", "[api]") {
    Usage usage;
    usage.promptTokens = 1000;
    usage.completionTokens = 500;
    usage.totalTokens = 1500;

    REQUIRE(usage.promptTokens == 1000);
    REQUIRE(usage.completionTokens == 500);
    REQUIRE(usage.totalTokens == 1500);
}

TEST_CASE("BedrockClient configuration", "[api]") {
    BedrockClient client;

    SECTION("Default configuration") {
        REQUIRE(client.getProviderName() == "bedrock");
        REQUIRE_FALSE(client.getModelName().empty());
        REQUIRE_FALSE(client.getRegion().empty());
    }

    SECTION("Set model") {
        client.setModel("us.anthropic.claude-opus-4-6-v1:0");
        REQUIRE(client.getModelName() == "us.anthropic.claude-opus-4-6-v1:0");
    }

    SECTION("Set region") {
        client.setRegion("eu-west-1");
        REQUIRE(client.getRegion() == "eu-west-1");
    }

    SECTION("Set max tokens") {
        client.setMaxTokens(8192);
    }

    SECTION("Set temperature") {
        client.setTemperature(0.5);
    }

    SECTION("setApiKey is ignored (uses AWS credentials)") {
        client.setApiKey("should-be-ignored");
        // No crash, key is ignored
    }
}

TEST_CASE("BedrockClient with region constructor", "[api]") {
    BedrockClient client("ap-northeast-1");
    REQUIRE(client.getProviderName() == "bedrock");
    REQUIRE(client.getRegion() == "ap-northeast-1");
}
