#include <catch2/catch_test_macros.hpp>
#include <claude/ui/XmlTagDispatcher.hpp>

using namespace claude;
using namespace claude::ui;

TEST_CASE("XmlTagDispatcher returns UserPrompt for plain text", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("Hello, world!") == DisplayMessage::Type::UserPrompt);
}

TEST_CASE("XmlTagDispatcher returns UserPrompt for text without recognized tags", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<unknown>stuff</unknown>") == DisplayMessage::Type::UserPrompt);
}

TEST_CASE("XmlTagDispatcher dispatches bash-stdout", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<bash-stdout>output here</bash-stdout>")
            == DisplayMessage::Type::UserBashOutput);
}

TEST_CASE("XmlTagDispatcher dispatches bash-stderr", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<bash-stderr>error msg</bash-stderr>")
            == DisplayMessage::Type::UserBashOutput);
}

TEST_CASE("XmlTagDispatcher dispatches command-message", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<command-message>/help</command-message>")
            == DisplayMessage::Type::UserCommandMessage);
}

TEST_CASE("XmlTagDispatcher dispatches local-command-stdout", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<local-command-stdout>output</local-command-stdout>")
            == DisplayMessage::Type::UserLocalCommandOutput);
}

TEST_CASE("XmlTagDispatcher dispatches teammate-message", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<teammate-message>Hello</teammate-message>")
            == DisplayMessage::Type::UserTeammateMessage);
}

TEST_CASE("XmlTagDispatcher dispatches task-notification", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<task-notification>done</task-notification>")
            == DisplayMessage::Type::UserTaskNotification);
}

TEST_CASE("XmlTagDispatcher dispatches mcp-resource-update", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<mcp-resource-update>updated</mcp-resource-update>")
            == DisplayMessage::Type::UserMcpResourceUpdate);
}

TEST_CASE("XmlTagDispatcher dispatches github-webhook-activity", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<github-webhook-activity>pr opened</github-webhook-activity>")
            == DisplayMessage::Type::UserGitHubWebhook);
}

TEST_CASE("XmlTagDispatcher dispatches fork-boilerplate", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<fork-boilerplate>content</fork-boilerplate>")
            == DisplayMessage::Type::UserForkBoilerplate);
}

TEST_CASE("XmlTagDispatcher dispatches cross-session-message", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<cross-session-message>hello</cross-session-message>")
            == DisplayMessage::Type::UserCrossSessionMessage);
}

TEST_CASE("XmlTagDispatcher dispatches channel with attributes", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch(R"(<channel name="general">msg</channel>)")
            == DisplayMessage::Type::UserChannelMessage);
}

TEST_CASE("XmlTagDispatcher dispatches user-memory-input", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<user-memory-input>remember this</user-memory-input>")
            == DisplayMessage::Type::UserMemoryInput);
}

TEST_CASE("XmlTagDispatcher dispatches bash-input", "[xml_dispatcher]") {
    REQUIRE(XmlTagDispatcher::dispatch("<bash-input>ls -la</bash-input>")
            == DisplayMessage::Type::UserBashInput);
}

TEST_CASE("parseFirstTag extracts content", "[xml_dispatcher]") {
    auto result = XmlTagDispatcher::parseFirstTag("<bash-stdout>hello world</bash-stdout>");
    REQUIRE(result.has_value());
    REQUIRE(result->tagName == "bash-stdout");
    REQUIRE(result->content == "hello world");
}

TEST_CASE("parseFirstTag extracts attributes", "[xml_dispatcher]") {
    auto result = XmlTagDispatcher::parseFirstTag(R"(<channel name="general" id="42">msg</channel>)");
    REQUIRE(result.has_value());
    REQUIRE(result->tagName == "channel");
    REQUIRE(result->attrs.at("name") == "general");
    REQUIRE(result->attrs.at("id") == "42");
    REQUIRE(result->content == "msg");
}

TEST_CASE("parseFirstTag returns nullopt for no tags", "[xml_dispatcher]") {
    auto result = XmlTagDispatcher::parseFirstTag("just plain text");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseFirstTag handles tag with no attributes", "[xml_dispatcher]") {
    auto result = XmlTagDispatcher::parseFirstTag("<command-message>/compact</command-message>");
    REQUIRE(result.has_value());
    REQUIRE(result->tagName == "command-message");
    REQUIRE(result->attrs.empty());
    REQUIRE(result->content == "/compact");
}

TEST_CASE("parseFirstTag handles tag without closing tag", "[xml_dispatcher]") {
    auto result = XmlTagDispatcher::parseFirstTag("<bash-stdout>some output without close");
    REQUIRE(result.has_value());
    REQUIRE(result->tagName == "bash-stdout");
    // Content should be empty when no closing tag found
    REQUIRE(result->content.empty());
}

TEST_CASE("parseFirstTag finds first tag among multiple", "[xml_dispatcher]") {
    auto result = XmlTagDispatcher::parseFirstTag(
        "prefix <bash-stdout>out1</bash-stdout> middle <bash-stderr>out2</bash-stderr>");
    REQUIRE(result.has_value());
    REQUIRE(result->tagName == "bash-stdout");
    REQUIRE(result->content == "out1");
}
