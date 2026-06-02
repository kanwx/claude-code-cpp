#include <catch2/catch_test_macros.hpp>
#include <claude/ui/UiMessageTypes.hpp>

using namespace claude;

TEST_CASE("P0 tool result subtypes create correctly", "[message_types]") {
    auto success = DisplayMessage::userToolSuccess("t1", "Read", "file contents");
    REQUIRE(success.type == DisplayMessage::Type::UserToolSuccess);
    REQUIRE(success.toolResult.toolUseId == "t1");
    REQUIRE(success.toolResult.toolName == "Read");
    REQUIRE(success.toolResult.result == "file contents");
    REQUIRE(success.toolResult.isError == false);
    REQUIRE(!success.messageId.empty());

    auto error = DisplayMessage::userToolError("t2", "Bash", "command failed");
    REQUIRE(error.type == DisplayMessage::Type::UserToolError);
    REQUIRE(error.toolResult.toolUseId == "t2");
    REQUIRE(error.toolResult.toolName == "Bash");
    REQUIRE(error.toolResult.result == "command failed");
    REQUIRE(error.toolResult.isError == true);
    REQUIRE(!error.messageId.empty());

    auto rejected = DisplayMessage::userToolRejected("t3", "Bash");
    REQUIRE(rejected.type == DisplayMessage::Type::UserToolRejected);
    REQUIRE(rejected.toolResult.toolUseId == "t3");
    REQUIRE(rejected.toolResult.toolName == "Bash");
    REQUIRE(rejected.toolResult.result == "Rejected");
    REQUIRE(!rejected.messageId.empty());

    auto canceled = DisplayMessage::userToolCanceled("t4", "Read");
    REQUIRE(canceled.type == DisplayMessage::Type::UserToolCanceled);
    REQUIRE(canceled.toolResult.toolUseId == "t4");
    REQUIRE(canceled.toolResult.toolName == "Read");
    REQUIRE(canceled.toolResult.result == "Canceled");
    REQUIRE(!canceled.messageId.empty());
}

TEST_CASE("Redacted thinking type creates correctly", "[message_types]") {
    auto msg = DisplayMessage::assistantRedactedThinking();
    REQUIRE(msg.type == DisplayMessage::Type::AssistantRedactedThinking);
    REQUIRE(!msg.messageId.empty());
}

TEST_CASE("P0 type height estimates", "[message_types]") {
    auto success = DisplayMessage::userToolSuccess("t1", "Read", "ok");
    REQUIRE(estimateMessageHeight(success, 80) == 1);

    auto error = DisplayMessage::userToolError("t2", "Bash", "fail");
    REQUIRE(estimateMessageHeight(error, 80) == 2);

    auto rejected = DisplayMessage::userToolRejected("t3", "Bash");
    REQUIRE(estimateMessageHeight(rejected, 80) == 1);

    auto canceled = DisplayMessage::userToolCanceled("t4", "Read");
    REQUIRE(estimateMessageHeight(canceled, 80) == 1);

    auto redacted = DisplayMessage::assistantRedactedThinking();
    REQUIRE(estimateMessageHeight(redacted, 80) == 1);
}
