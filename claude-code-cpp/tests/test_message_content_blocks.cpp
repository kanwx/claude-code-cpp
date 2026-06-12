#include <catch2/catch_test_macros.hpp>
#include "claude/core/ContentBlockParam.hpp"

using namespace claude;

TEST_CASE("ContentMessage user factory", "[ContentMessage]") {
    auto msg = ContentMessage::user("hello");
    REQUIRE(msg.role == MessageRole::User);
    REQUIRE(msg.content.size() == 1);
    REQUIRE(msg.content[0].type == ContentBlockParam::Text);
    REQUIRE(msg.content[0].text == "hello");
}

TEST_CASE("ContentMessage userBlocks with multiple blocks", "[ContentMessage]") {
    auto msg = ContentMessage::userBlocks({
        ContentBlockParam::makeText("first"),
        ContentBlockParam::makeText("second"),
    });
    REQUIRE(msg.role == MessageRole::User);
    REQUIRE(msg.content.size() == 2);
    REQUIRE(msg.content[0].type == ContentBlockParam::Text);
    REQUIRE(msg.content[0].text == "first");
    REQUIRE(msg.content[1].type == ContentBlockParam::Text);
    REQUIRE(msg.content[1].text == "second");
}

TEST_CASE("ContentMessage assistant factory", "[ContentMessage]") {
    auto msg = ContentMessage::assistant("response text");
    REQUIRE(msg.role == MessageRole::Assistant);
    REQUIRE(msg.content.size() == 1);
    REQUIRE(msg.content[0].type == ContentBlockParam::Text);
    REQUIRE(msg.content[0].text == "response text");
}

TEST_CASE("ContentMessage assistantBlocks with thinking+text+toolUse", "[ContentMessage]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeThinking("hmm...", "sig123"),
        ContentBlockParam::makeText("Here is my answer"),
        ContentBlockParam::makeToolUse("toolu_abc", "Read", Json::object()),
    });
    REQUIRE(msg.role == MessageRole::Assistant);
    REQUIRE(msg.content.size() == 3);
    REQUIRE(msg.content[0].type == ContentBlockParam::Thinking);
    REQUIRE(msg.content[1].type == ContentBlockParam::Text);
    REQUIRE(msg.content[2].type == ContentBlockParam::ToolUse);
}

TEST_CASE("ContentMessage system factory", "[ContentMessage]") {
    auto msg = ContentMessage::system("You are helpful");
    REQUIRE(msg.role == MessageRole::System);
    REQUIRE(msg.content.size() == 1);
    REQUIRE(msg.content[0].type == ContentBlockParam::Text);
    REQUIRE(msg.content[0].text == "You are helpful");
}

TEST_CASE("ContentMessage toolResultBlocks", "[ContentMessage]") {
    auto msg = ContentMessage::toolResultBlocks({
        ContentBlockParam::makeToolResult("toolu_abc", "file contents here"),
    });
    // Tool results go in user-role messages per Anthropic API convention
    REQUIRE(msg.role == MessageRole::User);
    REQUIRE(msg.content.size() == 1);
    REQUIRE(msg.content[0].type == ContentBlockParam::ToolResult);
    REQUIRE(msg.content[0].toolUseId == "toolu_abc");
    REQUIRE(msg.content[0].resultContent == "file contents here");
}

TEST_CASE("ContentMessage textContent concatenates text blocks", "[ContentMessage]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeThinking("internal", "sig"),
        ContentBlockParam::makeText("Hello"),
        ContentBlockParam::makeText("world"),
    });
    // textContent skips thinking, concatenates text blocks with space
    REQUIRE(msg.textContent() == "Hello world");
}

TEST_CASE("ContentMessage textContent with single text block", "[ContentMessage]") {
    auto msg = ContentMessage::assistant("solo");
    REQUIRE(msg.textContent() == "solo");
}

TEST_CASE("ContentMessage textContent with no text blocks", "[ContentMessage]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeThinking("only thinking", "sig"),
    });
    REQUIRE(msg.textContent().empty());
}

TEST_CASE("ContentMessage toolUseBlocks filters correctly", "[ContentMessage]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("I will use tools"),
        ContentBlockParam::makeToolUse("toolu_1", "Read", Json::object()),
        ContentBlockParam::makeToolUse("toolu_2", "Write", Json::object()),
    });
    auto tools = msg.toolUseBlocks();
    REQUIRE(tools.size() == 2);
    REQUIRE(tools[0].id == "toolu_1");
    REQUIRE(tools[0].name == "Read");
    REQUIRE(tools[1].id == "toolu_2");
    REQUIRE(tools[1].name == "Write");
}

TEST_CASE("ContentMessage toolUseBlocks with no tool calls", "[ContentMessage]") {
    auto msg = ContentMessage::assistant("just text");
    REQUIRE(msg.toolUseBlocks().empty());
}

TEST_CASE("ContentMessage hasContent with text", "[ContentMessage]") {
    auto msg = ContentMessage::assistant("hello");
    REQUIRE(msg.hasContent() == true);
}

TEST_CASE("ContentMessage hasContent with empty assistant", "[ContentMessage]") {
    ContentMessage msg;
    msg.role = MessageRole::Assistant;
    REQUIRE(msg.hasContent() == false);
}

TEST_CASE("ContentMessage hasContent with toolUse", "[ContentMessage]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeToolUse("toolu_x", "Bash", Json::object()),
    });
    REQUIRE(msg.hasContent() == true);
}

TEST_CASE("ContentMessage hasContent with toolResult", "[ContentMessage]") {
    auto msg = ContentMessage::toolResultBlocks({
        ContentBlockParam::makeToolResult("toolu_x", "output"),
    });
    REQUIRE(msg.hasContent() == true);
}

TEST_CASE("ContentMessage hasContent with RedactedThinking", "[ContentMessage]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeRedactedThinking("redacted_data"),
    });
    REQUIRE(msg.hasContent() == true);
}

TEST_CASE("ContentMessage hasToolCalls", "[ContentMessage]") {
    auto msg = ContentMessage::assistantBlocks({
        ContentBlockParam::makeText("text"),
        ContentBlockParam::makeToolUse("toolu_1", "Read", Json::object()),
    });
    REQUIRE(msg.hasToolCalls() == true);

    auto msgNoTools = ContentMessage::assistant("no tools");
    REQUIRE(msgNoTools.hasToolCalls() == false);
}

TEST_CASE("strip Thinking from ContentMessage vector", "[ContentMessage]") {
    std::vector<ContentMessage> messages;
    messages.push_back(ContentMessage::user("hello"));
    messages.push_back(ContentMessage::assistantBlocks({
        ContentBlockParam::makeThinking("think1", "sig1"),
        ContentBlockParam::makeText("answer"),
    }));
    messages.push_back(ContentMessage::assistantBlocks({
        ContentBlockParam::makeRedactedThinking("redacted"),
        ContentBlockParam::makeToolUse("toolu_1", "Bash", Json::object()),
    }));

    // Strip Thinking blocks from assistant messages, preserve RedactedThinking
    for (auto& msg : messages) {
        if (msg.role == MessageRole::Assistant) {
            auto it = std::remove_if(msg.content.begin(), msg.content.end(),
                [](const ContentBlockParam& b) {
                    return b.type == ContentBlockParam::Thinking;
                });
            msg.content.erase(it, msg.content.end());
        }
    }

    // First message unchanged
    REQUIRE(messages[0].content.size() == 1);
    REQUIRE(messages[0].content[0].text == "hello");

    // Second message: thinking stripped, text preserved
    REQUIRE(messages[1].content.size() == 1);
    REQUIRE(messages[1].content[0].type == ContentBlockParam::Text);
    REQUIRE(messages[1].content[0].text == "answer");

    // Third message: RedactedThinking preserved, toolUse preserved
    REQUIRE(messages[2].content.size() == 2);
    REQUIRE(messages[2].content[0].type == ContentBlockParam::RedactedThinking);
    REQUIRE(messages[2].content[1].type == ContentBlockParam::ToolUse);
}
