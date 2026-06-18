/// System Prompt Payload Regression Tests
///
/// Verifies that system prompt is correctly included in the final API request
/// payload for both Anthropic and OpenAI-compatible providers.
///
/// Tests:
///   Anthropic (official API):
///     1. System blocks at top-level, not in messages
///     2. Flat system string at top-level, not in messages
///     3. No system = no crash, no illegal fields
///     4. Empty system = no crash
///     5. No duplicate system
///     6. cache_control preserved
///   OpenAI-compatible / DeepSeek (custom base URL):
///     7. System in messages[0].role="system", no top-level system
///     8. No system = no crash
///     9. Empty system = no crash
///     10. No duplicate system
///     11. v2 signatures visible in payload
///   OpenAI client:
///     12. System stays in messages array
///     13. No system = no crash
///     14. Empty system = no crash
///   Integration:
///     15. Full pipeline: system blocks → Anthropic top-level format

#include <catch2/catch_test_macros.hpp>
#include <claude/api/AnthropicClient.hpp>
#include <claude/api/OpenAIClient.hpp>
#include <nlohmann/json.hpp>

using namespace claude;
using Json = nlohmann::json;

// ============================================================
// Helpers
// ============================================================

static Json makeSystemBlocks() {
    return Json::array({
        {{"type", "text"}, {"text", "You are a helpful assistant."}, {"cache_control", {{"type", "ephemeral"}}}},
        {{"type", "text"}, {"text", "# Output efficiency\nCRITICAL: Do NOT narrate your intent before tool calls.\n"}},
    });
}

static Json makeSystemFlatStr() {
    return "You are a helpful assistant.\n\n# Output efficiency\nCRITICAL: Do NOT narrate.";
}

static Json makeUserMsg(const std::string& text) {
    return {{"role", "user"}, {"content", text}};
}

// Helper: check that v2 OutputEfficiency signatures are present in JSON payload
static void requireV2SignaturesInPayload(const std::string& body) {
    REQUIRE(body.find("Output efficiency") != std::string::npos);
    REQUIRE(body.find("Do NOT narrate") != std::string::npos);
}

// ============================================================
// AnthropicClient::buildRequest tests
// ============================================================

TEST_CASE("Anthropic: system blocks at top-level, not in messages", "[system_prompt]") {
    AnthropicClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemBlocks()}});
    messages.push_back(makeUserMsg("Hello"));

    Json tools = Json::array();

    Json req = client.buildRequest(messages, tools);

    // System at top-level
    REQUIRE(req.contains("system"));
    REQUIRE(req["system"].is_array());
    REQUIRE(req["system"].size() == 2);

    // OutputEfficiency text present
    std::string sysText;
    for (auto& b : req["system"]) {
        if (b.contains("text")) sysText += b["text"].get<std::string>();
    }
    REQUIRE(sysText.find("Output efficiency") != std::string::npos);
    REQUIRE(sysText.find("Do NOT narrate") != std::string::npos);

    // Messages array must NOT contain system
    REQUIRE(req.contains("messages"));
    for (const auto& m : req["messages"]) {
        REQUIRE(m["role"] != "system");
    }
    REQUIRE(req["messages"].size() == 1);
    REQUIRE(req["messages"][0]["role"] == "user");
}

TEST_CASE("Anthropic: flat system string at top-level, not in messages", "[system_prompt]") {
    AnthropicClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemFlatStr()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // System at top-level (converted to blocks)
    REQUIRE(req.contains("system"));
    REQUIRE(req["system"].is_array());
    // buildSystemPromptBlocks splits flat string into text blocks
    REQUIRE(req["system"].size() >= 1);

    // First block contains the text
    std::string firstText = req["system"][0]["text"].get<std::string>();
    REQUIRE(firstText.find("Do NOT narrate") != std::string::npos);

    // Not in messages
    for (const auto& m : req["messages"]) {
        REQUIRE(m["role"] != "system");
    }
}

TEST_CASE("Anthropic: no system message — no crash, no illegal fields", "[system_prompt]") {
    AnthropicClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // No system field at all
    REQUIRE_FALSE(req.contains("system"));

    // Messages intact
    REQUIRE(req["messages"].size() == 1);
    REQUIRE(req["messages"][0]["role"] == "user");
}

TEST_CASE("Anthropic: empty system content — no crash", "[system_prompt]") {
    AnthropicClient client;
    client.setApiKey("sk-test");

    // system role with empty content array
    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", Json::array()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // Should not crash; system omitted since empty
    REQUIRE_FALSE(req.contains("system"));
    REQUIRE(req["messages"].size() == 1);
}

TEST_CASE("Anthropic: no duplicate system prompt", "[system_prompt]") {
    AnthropicClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemBlocks()}});
    messages.push_back({{"role", "system"}, {"content", "Another system message"}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // buildRequest takes first system, ignores subsequent ones
    REQUIRE(req.contains("system"));
    REQUIRE(req["system"].is_array());
    REQUIRE(req["system"].size() == 2);  // Only first system's blocks

    // No system in messages
    for (const auto& m : req["messages"]) {
        REQUIRE(m["role"] != "system");
    }
}

TEST_CASE("Anthropic: system blocks with cache_control preserved", "[system_prompt]") {
    AnthropicClient client;
    client.setApiKey("sk-test");

    auto blocks = makeSystemBlocks();
    // First block has ephemeral cache_control
    REQUIRE(blocks[0].contains("cache_control"));

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", blocks}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // cache_control preserved in output
    REQUIRE(req["system"][0].contains("cache_control"));
    REQUIRE(req["system"][0]["cache_control"]["type"] == "ephemeral");
}

// ============================================================
// OpenAIClient::buildRequest tests
// ============================================================

TEST_CASE("OpenAI: system stays in messages array", "[system_prompt]") {
    OpenAIClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", "You are a helpful assistant."}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // OpenAI keeps system as a message in the messages array
    REQUIRE(req.contains("messages"));
    REQUIRE(req["messages"].size() == 2);
    REQUIRE(req["messages"][0]["role"] == "system");
    REQUIRE(req["messages"][0]["content"] == "You are a helpful assistant.");
    REQUIRE(req["messages"][1]["role"] == "user");
}

TEST_CASE("OpenAI: system blocks stay in messages array", "[system_prompt]") {
    OpenAIClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemBlocks()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // System stays in messages as-is
    REQUIRE(req["messages"].size() == 2);
    REQUIRE(req["messages"][0]["role"] == "system");
    REQUIRE(req["messages"][0]["content"].is_array());
    REQUIRE(req["messages"][0]["content"].size() == 2);
}

TEST_CASE("OpenAI: no system message — no crash", "[system_prompt]") {
    OpenAIClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    REQUIRE(req["messages"].size() == 1);
    REQUIRE(req["messages"][0]["role"] == "user");
}

TEST_CASE("OpenAI: empty system content — no crash", "[system_prompt]") {
    OpenAIClient client;
    client.setApiKey("sk-test");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", Json::array()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // Empty system passed through
    REQUIRE(req["messages"].size() == 2);
    REQUIRE(req["messages"][0]["role"] == "system");
}

// ============================================================
// Integration-style: verify the full transformation pipeline
// ============================================================

TEST_CASE("Full pipeline: system blocks → Anthropic top-level format", "[system_prompt]") {
    // Simulates what AgentLoop::buildApiRequest now does:
    //   1. request["system"] is wrapped as {role:"system", content: [...]}
    //   2. Inserted at front of request["messages"]
    //   3. AnthropicClient::buildRequest extracts to top-level

    AnthropicClient client;
    client.setApiKey("sk-test");

    // Step 1: Simulate AgentLoop output — system wrapped in messages
    Json sysBlocks = makeSystemBlocks();
    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", sysBlocks}});
    messages.push_back(makeUserMsg("Analyze this codebase"));
    messages.push_back({{"role", "assistant"}, {"content", "I'll start by reading the key files."}});
    messages.push_back({{"role", "user"}, {"content", "<tool-result>...</tool-result>"}});

    // Step 2: AnthropicClient extracts system to top-level
    Json tools = Json::array({{{"name", "Read"}}});
    Json req = client.buildRequest(messages, tools);

    // Step 3: Verify final Anthropic API format
    REQUIRE(req["model"].is_string());
    REQUIRE(req["max_tokens"].is_number());
    REQUIRE(req.contains("system"));
    REQUIRE(req["system"].is_array());
    REQUIRE(req["system"].size() == 2);

    // No system in messages
    for (const auto& m : req["messages"]) {
        INFO("Message role: " + m["role"].get<std::string>());
        REQUIRE(m["role"] != "system");
    }

    // Conversation messages preserved in order
    REQUIRE(req["messages"].size() == 3);  // user, assistant, user
    REQUIRE(req["messages"][0]["role"] == "user");
    REQUIRE(req["messages"][1]["role"] == "assistant");
    REQUIRE(req["messages"][2]["role"] == "user");

    // OutputEfficiency content is present in system
    std::string allSysText;
    for (auto& b : req["system"]) {
        if (b.contains("text")) allSysText += b["text"].get<std::string>();
    }
    REQUIRE(allSysText.find("Output efficiency") != std::string::npos);
    REQUIRE(allSysText.find("Do NOT narrate") != std::string::npos);
}

// ============================================================
// DeepSeek / custom base URL (OpenAI-compatible protocol)
// ============================================================

TEST_CASE("DeepSeek: system in messages[0], no top-level system field", "[system_prompt][deepseek]") {
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setBaseUrl("https://api.deepseek.com/v1");  // custom base URL

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemBlocks()}});
    messages.push_back(makeUserMsg("Analyze this codebase"));

    Json req = client.buildRequest(messages, Json::array());

    // No top-level system field
    REQUIRE_FALSE(req.contains("system"));

    // System is in messages[0]
    REQUIRE(req["messages"].size() == 2);
    REQUIRE(req["messages"][0]["role"] == "system");
    REQUIRE(req["messages"][0]["content"].is_array());
    REQUIRE(req["messages"][0]["content"].size() == 2);

    // Second message is user
    REQUIRE(req["messages"][1]["role"] == "user");

    // v2 signatures in serialized body
    std::string body = req.dump();
    requireV2SignaturesInPayload(body);
}

TEST_CASE("DeepSeek: system in messages[0] with flat string content", "[system_prompt][deepseek]") {
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setBaseUrl("https://api.deepseek.com/v1");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemFlatStr()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // No top-level system
    REQUIRE_FALSE(req.contains("system"));

    // System in messages
    REQUIRE(req["messages"].size() == 2);
    REQUIRE(req["messages"][0]["role"] == "system");
    // Content passed through as string
    REQUIRE(req["messages"][0]["content"].is_string());

    std::string body = req.dump();
    requireV2SignaturesInPayload(body);
}

TEST_CASE("DeepSeek: no system message — no crash, no illegal fields", "[system_prompt][deepseek]") {
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setBaseUrl("https://api.deepseek.com/v1");

    Json messages = Json::array();
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    REQUIRE_FALSE(req.contains("system"));
    REQUIRE(req["messages"].size() == 1);
    REQUIRE(req["messages"][0]["role"] == "user");
}

TEST_CASE("DeepSeek: empty system content — no crash", "[system_prompt][deepseek]") {
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setBaseUrl("https://api.deepseek.com/v1");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", Json::array()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // Empty system passed through in messages
    REQUIRE(req["messages"].size() == 2);
    REQUIRE(req["messages"][0]["role"] == "system");
    REQUIRE(req["messages"][0]["content"].is_array());
    REQUIRE(req["messages"][0]["content"].empty());
}

TEST_CASE("DeepSeek: no duplicate system prompt", "[system_prompt][deepseek]") {
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setBaseUrl("https://api.deepseek.com/v1");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemFlatStr()}});
    // Only one system message inserted by AgentLoop — verify no duplication
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // System appears exactly once
    size_t sysCount = 0;
    for (const auto& m : req["messages"]) {
        if (m["role"] == "system") sysCount++;
    }
    REQUIRE(sysCount == 1);

    // v2 signatures present
    std::string body = req.dump();
    requireV2SignaturesInPayload(body);
}

TEST_CASE("DeepSeek: cache_control blocks preserved in system message", "[system_prompt][deepseek]") {
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setBaseUrl("https://api.deepseek.com/v1");

    auto blocks = makeSystemBlocks();
    REQUIRE(blocks[0].contains("cache_control"));

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", blocks}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // cache_control preserved in messages[0].content
    REQUIRE(req["messages"][0]["content"][0].contains("cache_control"));
    REQUIRE(req["messages"][0]["content"][0]["cache_control"]["type"] == "ephemeral");
}

TEST_CASE("Anthropic: switching back to official API still uses top-level system", "[system_prompt][deepseek]") {
    // Verify that when base URL is NOT custom, we still get top-level system
    AnthropicClient client;
    client.setApiKey("sk-test");
    // Default base URL = api.anthropic.com → isCustomBaseUrl_ = false

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemBlocks()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // Top-level system present
    REQUIRE(req.contains("system"));
    REQUIRE(req["system"].is_array());

    // No system in messages
    for (const auto& m : req["messages"]) {
        REQUIRE(m["role"] != "system");
    }
}

// ============================================================
// Explicit ApiProtocol override (Anthropic-compatible proxy)
// ============================================================

TEST_CASE("Explicit ApiProtocol::Anthropic overrides custom base URL", "[system_prompt]") {
    // Scenario: Anthropic-compatible proxy at a custom base URL.
    // The automatic derivation would pick OpenAICompatible, but the user
    // explicitly sets ApiProtocol::Anthropic because the proxy expects
    // the system prompt at the top-level "system" field.
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setBaseUrl("https://custom-proxy.internal/v1");  // custom URL
    client.setApiProtocol(ApiProtocol::Anthropic);           // explicit override

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemBlocks()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // Despite custom base URL, system is at top-level per explicit protocol
    REQUIRE(req.contains("system"));
    REQUIRE(req["system"].is_array());
    REQUIRE(req["system"].size() == 2);

    // No system in messages
    for (const auto& m : req["messages"]) {
        REQUIRE(m["role"] != "system");
    }
}

TEST_CASE("setApiProtocol survives base URL changes", "[system_prompt]") {
    // Once explicitly set, protocol persists even after base URL changes
    AnthropicClient client;
    client.setApiKey("sk-test");
    client.setApiProtocol(ApiProtocol::Anthropic);
    client.setBaseUrl("https://api.deepseek.com/v1");  // would derive OpenAICompatible

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", makeSystemFlatStr()}});
    messages.push_back(makeUserMsg("Hello"));

    Json req = client.buildRequest(messages, Json::array());

    // Explicit protocol wins over base URL inference
    REQUIRE(req.contains("system"));
    REQUIRE_FALSE(req["messages"][0]["role"] == "system");
}
