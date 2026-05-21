#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <claude/api/RetryableClient.hpp>
#include <claude/ui/MessagePipeline.hpp>

using json = nlohmann::json;
using namespace claude;

TEST_CASE("FallbackTriggered exception carries model info", "[fallback]") {
    Json msgs = json::array();
    FallbackTriggered ex("claude-opus-4-7", "claude-sonnet-4-6", msgs);
    REQUIRE(ex.fromModel == "claude-opus-4-7");
    REQUIRE(ex.toModel == "claude-sonnet-4-6");
    REQUIRE(ex.strippedMessages.is_array());
}

TEST_CASE("FallbackTriggered exception what() contains model names", "[fallback]") {
    Json msgs = json::array();
    FallbackTriggered ex("model-a", "model-b", msgs);
    String what = ex.what();
    REQUIRE(what.find("model-a") != String::npos);
    REQUIRE(what.find("model-b") != String::npos);
}

TEST_CASE("StreamEvent has Tombstone type", "[fallback]") {
    StreamEvent event;
    event.type = StreamEvent::Type::Tombstone;
    event.fallbackFromModel = "opus";
    event.fallbackToModel = "sonnet";
    REQUIRE(event.type == StreamEvent::Type::Tombstone);
    REQUIRE(event.fallbackFromModel == "opus");
    REQUIRE(event.fallbackToModel == "sonnet");
}

TEST_CASE("RetryableClient overload threshold is 3", "[fallback]") {
    REQUIRE(RetryableClient::OVERLOAD_THRESHOLD == 3);
}
