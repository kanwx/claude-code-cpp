/**
 * P6-P3a: Token display in TurnDuration — state machine + format tests.
 *
 * State machine tests (3-5): exercise handleDisplayEvent + lastTurnOutputTokens() getter.
 * Format tests (1-2, 6-8): exercise buildTurnDurationText() static method.
 */
#include <catch2/catch_test_macros.hpp>
#include <claude/ui/FtxuiRepl.hpp>
#include <claude/stream/DisplayEvent.hpp>

using namespace claude;

// ---- helpers ----------------------------------------------------------------

static DisplayEvent makeUsageUpdate(int64_t outputTokens) {
    DisplayEvent ev{.type = DisplayEventType::TurnMetadata};
    ev.metadata.outputTokens = outputTokens;
    return ev;
}

static DisplayEvent makeAnswerEnd() {
    // AnswerEnd TurnMetadata: durationStr + isStreaming=false.
    // outputTokens defaults to 0 — must NOT overwrite captured value from UsageUpdate.
    DisplayEvent ev{.type = DisplayEventType::TurnMetadata};
    ev.metadata.durationStr = "23s";
    ev.metadata.isStreaming = false;
    return ev;
}

static DisplayEvent makeAnswerStart() {
    return {.type = DisplayEventType::AnswerStart};
}

static DisplayEvent makeEmptyTurnMetadata() {
    // Simulates a TurnMetadata with all defaults (outputTokens=0).
    return {.type = DisplayEventType::TurnMetadata};
}

// ============================================================================
// Format tests (tests 1-2, 6-7)
// Exercise buildTurnDurationText() directly — no FtxuiRepl state needed.
// ============================================================================

TEST_CASE("P3a: outputTokens > 0 — TurnDuration includes token suffix", "[p3a][format]") {
    // 40100 tokens → "40.1K tokens"
    String text = FtxuiRepl::buildTurnDurationText("Baked", 45, 8, 40100);
    CHECK(text.find("40.1K tokens") != String::npos);
}

TEST_CASE("P3a: outputTokens == 0 — no token suffix", "[p3a][format]") {
    String text = FtxuiRepl::buildTurnDurationText("Baked", 45, 8, 0);
    CHECK(text.find("tokens") == String::npos);
}

TEST_CASE("P3a: tool count before tokens in format string", "[p3a][format]") {
    String text = FtxuiRepl::buildTurnDurationText("Baked", 45, 3, 4200);
    auto toolPos = text.find("3 tools");
    auto tokenPos = text.find("4.2K tokens");
    CHECK(toolPos != String::npos);
    CHECK(tokenPos != String::npos);
    CHECK(toolPos < tokenPos);
}

TEST_CASE("P3a: 0 tools + tokens — verb and elapsed only", "[p3a][format]") {
    String text = FtxuiRepl::buildTurnDurationText("Worked", 23, 0, 4200);
    CHECK(text.find("tool") == String::npos);
    CHECK(text.find("4.2K tokens") != String::npos);
    CHECK(text.find("Worked for 23s") == 0);  // starts with verb + elapsed
}

TEST_CASE("P3a: millions format correctly", "[p3a][format]") {
    String text = FtxuiRepl::buildTurnDurationText("Crunched", 120, 5, 1'500'000);
    CHECK(text.find("1.5M tokens") != String::npos);
}

TEST_CASE("P3a: small tokens (< 1000) formatted without K suffix", "[p3a][format]") {
    String text = FtxuiRepl::buildTurnDurationText("Brewed", 10, 1, 500);
    CHECK(text.find("500 tokens") != String::npos);
}

TEST_CASE("P3a: P1d tool count format unchanged", "[p3a][format]") {
    // 1 tool → singular
    String t1 = FtxuiRepl::buildTurnDurationText("Baked", 45, 1, 0);
    CHECK(t1.find("1 tool") != String::npos);

    // 3 tools → plural
    String t3 = FtxuiRepl::buildTurnDurationText("Baked", 45, 3, 0);
    CHECK(t3.find("3 tools") != String::npos);
}

// ============================================================================
// State machine tests (tests 3-5)
// Exercise handleDisplayEvent synchronously — no screen needed.
// ============================================================================

TEST_CASE("P3a: AnswerStart resets lastTurnOutputTokens_", "[p3a][state]") {
    FtxuiRepl repl;

    // Turn 1: UsageUpdate captured
    repl.handleDisplayEvent(makeUsageUpdate(40100));
    CHECK(repl.lastTurnOutputTokens() == 40100);

    // AnswerStart resets to 0
    repl.handleDisplayEvent(makeAnswerStart());
    CHECK(repl.lastTurnOutputTokens() == 0);
}

TEST_CASE("P3a: AnswerEnd zero outputTokens does not overwrite UsageUpdate capture", "[p3a][state]") {
    FtxuiRepl repl;

    repl.handleDisplayEvent(makeUsageUpdate(40100));
    CHECK(repl.lastTurnOutputTokens() == 40100);

    // AnswerEnd TurnMetadata has outputTokens=0 (default) — must NOT overwrite
    repl.handleDisplayEvent(makeAnswerEnd());
    CHECK(repl.lastTurnOutputTokens() == 40100);
}

TEST_CASE("P3a: no UsageUpdate in current turn — no leakage from previous turn", "[p3a][state]") {
    FtxuiRepl repl;

    // Turn 1: full lifecycle
    repl.handleDisplayEvent(makeAnswerStart());
    CHECK(repl.lastTurnOutputTokens() == 0);
    repl.handleDisplayEvent(makeUsageUpdate(25000));
    CHECK(repl.lastTurnOutputTokens() == 25000);
    repl.handleDisplayEvent(makeAnswerEnd());
    CHECK(repl.lastTurnOutputTokens() == 25000);  // AnswerEnd doesn't clear (guard: > 0)

    // Turn 2: AnswerStart resets — this is the leakage guard
    repl.handleDisplayEvent(makeAnswerStart());
    CHECK(repl.lastTurnOutputTokens() == 0);

    // Turn 2 has NO UsageUpdate — must stay 0
    repl.handleDisplayEvent(makeAnswerEnd());
    CHECK(repl.lastTurnOutputTokens() == 0);
}

TEST_CASE("P3a: empty TurnMetadata does not set tokens to negative or garbage", "[p3a][state]") {
    FtxuiRepl repl;

    // UsageUpdate captured
    repl.handleDisplayEvent(makeUsageUpdate(40100));
    CHECK(repl.lastTurnOutputTokens() == 40100);

    // Empty TurnMetadata (outputTokens=0, all defaults) — must not overwrite
    repl.handleDisplayEvent(makeEmptyTurnMetadata());
    CHECK(repl.lastTurnOutputTokens() == 40100);
}

TEST_CASE("P3a: zero UsageUpdate does not overwrite positive capture", "[p3a][state]") {
    FtxuiRepl repl;

    repl.handleDisplayEvent(makeUsageUpdate(40100));
    CHECK(repl.lastTurnOutputTokens() == 40100);

    // A subsequent UsageUpdate with 0 tokens — edge case, should not overwrite
    repl.handleDisplayEvent(makeUsageUpdate(0));
    CHECK(repl.lastTurnOutputTokens() == 40100);
}

TEST_CASE("P3a: multiple UsageUpdates keep the last positive value", "[p3a][state]") {
    FtxuiRepl repl;

    repl.handleDisplayEvent(makeUsageUpdate(10000));
    CHECK(repl.lastTurnOutputTokens() == 10000);

    // Second UsageUpdate with more tokens (streaming token count grows)
    repl.handleDisplayEvent(makeUsageUpdate(25000));
    CHECK(repl.lastTurnOutputTokens() == 25000);

    // Third UsageUpdate (final count from API)
    repl.handleDisplayEvent(makeUsageUpdate(40100));
    CHECK(repl.lastTurnOutputTokens() == 40100);
}
