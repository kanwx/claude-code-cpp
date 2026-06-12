#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "claude/core/ContentBlockParam.hpp"

using namespace claude;
using Json = nlohmann::json;

TEST_CASE("migrateLegacySession converts flat content string to array with text block") {
    Json msg = {{"role", "user"}, {"content", "Hello world"}};
    migrateLegacySession(msg);

    REQUIRE(msg["content"].is_array());
    REQUIRE(msg["content"].size() == 1);
    REQUIRE(msg["content"][0]["type"] == "text");
    REQUIRE(msg["content"][0]["text"] == "Hello world");
    REQUIRE(msg["_format_version"] == 2);
}

TEST_CASE("migrateLegacySession converts tool_calls to tool_use blocks and removes tool_calls field") {
    Json msg = {
        {"role", "assistant"},
        {"content", "I'll help you with that."},
        {"tool_calls", Json::array({
            {{"id", "toolu_123"}, {"name", "Read"}, {"input", Json::object({{"file_path", "/tmp/a.txt"}})}}
        })}
    };
    migrateLegacySession(msg);

    REQUIRE(msg["content"].is_array());
    REQUIRE(msg["content"].size() == 2); // text + tool_use

    // Text block
    REQUIRE(msg["content"][0]["type"] == "text");

    // Tool use block
    REQUIRE(msg["content"][1]["type"] == "tool_use");
    REQUIRE(msg["content"][1]["id"] == "toolu_123");
    REQUIRE(msg["content"][1]["name"] == "Read");
    REQUIRE(msg["content"][1]["input"]["file_path"] == "/tmp/a.txt");

    // Old field removed
    REQUIRE_FALSE(msg.contains("tool_calls"));
    REQUIRE(msg["_format_version"] == 2);
}

TEST_CASE("migrateLegacySession converts thinking + signature to thinking block and removes fields") {
    Json msg = {
        {"role", "assistant"},
        {"content", "The answer is 42."},
        {"thinking", "Let me think about this..."},
        {"signature", "sig_abc123"}
    };
    migrateLegacySession(msg);

    REQUIRE(msg["content"].is_array());
    REQUIRE(msg["content"].size() == 2); // thinking + text

    // Thinking block comes first
    REQUIRE(msg["content"][0]["type"] == "thinking");
    REQUIRE(msg["content"][0]["thinking"] == "Let me think about this...");
    REQUIRE(msg["content"][0]["signature"] == "sig_abc123");

    // Text block
    REQUIRE(msg["content"][1]["type"] == "text");

    // Old fields removed
    REQUIRE_FALSE(msg.contains("thinking"));
    REQUIRE_FALSE(msg.contains("signature"));
    REQUIRE(msg["_format_version"] == 2);
}

TEST_CASE("migrateLegacySession converts tool_results to tool_result blocks and removes field") {
    Json msg = {
        {"role", "user"},
        {"content", ""},
        {"tool_results", Json::array({
            {{"tool_use_id", "toolu_123"}, {"content", "file contents here"}, {"is_error", false}}
        })}
    };
    migrateLegacySession(msg);

    REQUIRE(msg["content"].is_array());
    REQUIRE(msg["content"].size() == 1); // only tool_result (empty text skipped)

    REQUIRE(msg["content"][0]["type"] == "tool_result");
    REQUIRE(msg["content"][0]["tool_use_id"] == "toolu_123");
    REQUIRE(msg["content"][0]["content"] == "file contents here");
    REQUIRE_FALSE(msg["content"][0].contains("is_error")); // false = omitted

    // Old field removed
    REQUIRE_FALSE(msg.contains("tool_results"));
    REQUIRE(msg["_format_version"] == 2);
}

TEST_CASE("migrateLegacySession converts redacted_thinking to redacted_thinking blocks and removes field") {
    Json msg = {
        {"role", "assistant"},
        {"content", "Response text"},
        {"redacted_thinking", Json::array({
            {{"data", "redacted_data_1"}},
            {{"data", "redacted_data_2"}}
        })}
    };
    migrateLegacySession(msg);

    REQUIRE(msg["content"].is_array());
    // text + 2 redacted_thinking
    bool foundRedacted1 = false, foundRedacted2 = false;
    for (auto& b : msg["content"]) {
        if (b["type"] == "redacted_thinking") {
            if (b["data"] == "redacted_data_1") foundRedacted1 = true;
            if (b["data"] == "redacted_data_2") foundRedacted2 = true;
        }
    }
    REQUIRE(foundRedacted1);
    REQUIRE(foundRedacted2);

    // Old field removed
    REQUIRE_FALSE(msg.contains("redacted_thinking"));
    REQUIRE(msg["_format_version"] == 2);
}

TEST_CASE("migrateLegacySession skips already-migrated format with _format_version==2") {
    Json msg = {
        {"role", "user"},
        {"content", Json::array({{"type", "text"}, {"text", "Hello"}})},
        {"_format_version", 2}
    };
    Json original = msg; // snapshot
    migrateLegacySession(msg);

    // Should be unchanged
    REQUIRE(msg == original);
}

TEST_CASE("migrateLegacySession skips if content is already an array (no _format_version)") {
    Json msg = {
        {"role", "user"},
        {"content", Json::array({{"type", "text"}, {"text", "Hello"}})}
    };
    Json original = msg;
    migrateLegacySession(msg);

    // Should be unchanged (content is already array, not string)
    REQUIRE(msg == original);
}

TEST_CASE("migrateLegacySession handles OpenAI format tool_calls with function.name/arguments") {
    Json toolCall = Json::object();
    toolCall["id"] = "call_abc";
    toolCall["function"] = {{"name", "Read"}, {"arguments", "{\"file_path\":\"/tmp/test.txt\"}"}};

    Json msg = {{"role", "assistant"}, {"content", "Let me read that file."}};
    msg["tool_calls"] = Json::array({toolCall});
    migrateLegacySession(msg);

    REQUIRE(msg["content"].is_array());

    // Find the tool_use block
    bool found = false;
    for (auto& b : msg["content"]) {
        if (b["type"] == "tool_use") {
            REQUIRE(b["id"] == "call_abc");
            REQUIRE(b["name"] == "Read");
            REQUIRE(b["input"]["file_path"] == "/tmp/test.txt");
            found = true;
        }
    }
    REQUIRE(found);
    REQUIRE_FALSE(msg.contains("tool_calls"));
}

TEST_CASE("migrateLegacySession sets _format_version to 2 after migration") {
    Json msg = {{"role", "user"}, {"content", "test"}};
    REQUIRE_FALSE(msg.contains("_format_version"));

    migrateLegacySession(msg);

    REQUIRE(msg["_format_version"] == 2);
}

TEST_CASE("migrateLegacySession handles tool_results with tool_call_id alias") {
    Json msg = {
        {"role", "user"},
        {"content", ""},
        {"tool_results", Json::array({
            {{"tool_call_id", "call_xyz"}, {"content", "result data"}}
        })}
    };
    migrateLegacySession(msg);

    REQUIRE(msg["content"][0]["tool_use_id"] == "call_xyz");
    REQUIRE(msg["content"][0]["content"] == "result data");
}

TEST_CASE("migrateLegacySession handles tool_results with is_error=true") {
    Json msg = {
        {"role", "user"},
        {"content", ""},
        {"tool_results", Json::array({
            {{"tool_use_id", "toolu_err"}, {"content", "Error: file not found"}, {"is_error", true}}
        })}
    };
    migrateLegacySession(msg);

    REQUIRE(msg["content"][0]["is_error"] == true);
}

TEST_CASE("migrateLegacySession skips thinking for non-assistant role") {
    Json msg = {
        {"role", "user"},
        {"content", "Hello"},
        {"thinking", "Should not be included"},
        {"signature", "sig"}
    };
    migrateLegacySession(msg);

    // No thinking block should appear
    bool hasThinking = false;
    for (auto& b : msg["content"]) {
        if (b["type"] == "thinking") hasThinking = true;
    }
    REQUIRE_FALSE(hasThinking);
    // thinking/signature fields still removed
    REQUIRE_FALSE(msg.contains("thinking"));
    REQUIRE_FALSE(msg.contains("signature"));
}
