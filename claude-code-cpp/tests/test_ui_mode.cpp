/// UI Mode Selection & Plain CLI Rendering Tests
///
/// Tests:
///   1. decideUiMode() — startup mode selection logic
///   2. plain CLI renderer — no Thinking spam, no text interleaving

#include <catch2/catch_test_macros.hpp>
#include "claude/bootstrap/UiMode.hpp"
#include "claude/stream/DisplayEvent.hpp"
#include "claude/ui/ContentBlockRenderer.hpp"
#include <sstream>
#include <iostream>

using namespace claude;

// ============================================================
// Helper: strip ANSI escape sequences for content assertions
// ============================================================

static std::string stripAnsi(const std::string& s) {
    std::string out;
    bool inEscape = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\x1b') {
            inEscape = true;
            continue;
        }
        if (inEscape) {
            if ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z')) {
                inEscape = false;
            }
            continue;
        }
        out += s[i];
    }
    return out;
}

// ============================================================
// UI Mode Decision Tests
// ============================================================

TEST_CASE("decideUiMode: no args + tty → FTXUI", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/true,
        /*explicitNoFtxui=*/false,
        /*explicitInteractive=*/false,
        /*hasPrompt=*/false,
        /*stdoutIsTty=*/true);
    CHECK(d.interactive == true);
    CHECK(d.useFtxui == true);
    CHECK(d.mode == "ftxui");
    CHECK(d.reason == "interactive_tty_default");
}

TEST_CASE("decideUiMode: -p → headless", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/true,
        /*explicitNoFtxui=*/false,
        /*explicitInteractive=*/false,
        /*hasPrompt=*/true,
        /*stdoutIsTty=*/true);
    CHECK(d.interactive == false);
    CHECK(d.useFtxui == false);
    CHECK(d.mode == "headless");
    CHECK(d.reason == "print_mode");
}

TEST_CASE("decideUiMode: -i + -p → headless (prompt wins)", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/true,
        /*explicitNoFtxui=*/false,
        /*explicitInteractive=*/true,
        /*hasPrompt=*/true,
        /*stdoutIsTty=*/true);
    // -p always forces non-interactive headless mode
    CHECK(d.interactive == false);
    CHECK(d.useFtxui == false);
    CHECK(d.mode == "headless");
    CHECK(d.reason == "print_mode");
}

TEST_CASE("decideUiMode: --no-ftxui + tty → plain", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/true,
        /*explicitNoFtxui=*/true,
        /*explicitInteractive=*/false,
        /*hasPrompt=*/false,
        /*stdoutIsTty=*/true);
    CHECK(d.interactive == true);
    CHECK(d.useFtxui == false);
    CHECK(d.mode == "plain");
    CHECK(d.reason == "no_ftxui_flag");
}

TEST_CASE("decideUiMode: no args + stdout pipe → fallback", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/true,
        /*explicitNoFtxui=*/false,
        /*explicitInteractive=*/false,
        /*hasPrompt=*/false,
        /*stdoutIsTty=*/false);
    CHECK(d.interactive == true);
    CHECK(d.useFtxui == false);
    CHECK(d.mode == "plain");
    CHECK(d.reason == "stdout_not_tty");
}

TEST_CASE("decideUiMode: no args + no FTXUI build → plain", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/false,
        /*explicitNoFtxui=*/false,
        /*explicitInteractive=*/false,
        /*hasPrompt=*/false,
        /*stdoutIsTty=*/true);
    CHECK(d.interactive == true);
    CHECK(d.useFtxui == false);
    CHECK(d.mode == "plain");
}

TEST_CASE("decideUiMode: -i flag + tty → FTXUI", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/true,
        /*explicitNoFtxui=*/false,
        /*explicitInteractive=*/true,
        /*hasPrompt=*/false,
        /*stdoutIsTty=*/true);
    CHECK(d.interactive == true);
    CHECK(d.useFtxui == true);
    CHECK(d.mode == "ftxui");
}

TEST_CASE("decideUiMode: --no-ftxui + stdout pipe → no_ftxui_flag wins", "[UiMode]") {
    auto d = decideUiMode(
        /*hasFtxuiBuild=*/true,
        /*explicitNoFtxui=*/true,
        /*explicitInteractive=*/false,
        /*hasPrompt=*/false,
        /*stdoutIsTty=*/false);
    CHECK(d.interactive == true);
    CHECK(d.useFtxui == false);
    CHECK(d.mode == "plain");
    // no_ftxui_flag is checked first, so it's the reported reason
    CHECK(d.reason == "no_ftxui_flag");
}

// ============================================================
// Plain CLI Rendering Replay Test
// ============================================================

// Miniature replica of the ANSI renderer logic from AgentRunner.cpp,
// capturing output to a stringstream instead of stdout.
// This mirrors the actual rendering path (after Fix 3) without
// involving the full StreamBuffer → DisplayEvent pipeline.

struct PlainCliRenderer {
    std::ostringstream out;
    bool spinnerRunning = true;  // simulate spinner on stderr (not captured)

    void feed(const DisplayEvent& event) {
        switch (event.type) {
            case DisplayEventType::AnswerStart:
                // Stop spinner, move past its stderr line
                spinnerRunning = false;
                out << "\n";
                break;

            case DisplayEventType::TextParagraph:
            case DisplayEventType::TextPartial:
                out << event.text;
                break;

            case DisplayEventType::ThinkingBlock:
                // Suppressed: Spinner handles thinking status on stderr
                break;

            case DisplayEventType::ToolProgress:
                // Suppressed in ANSI mode
                break;

            case DisplayEventType::ToolResult: {
                ContentBlock cb;
                cb.type = ContentBlock::ToolResult;
                cb.toolName = event.toolName;
                cb.summary = event.summary;
                out << ContentBlockRenderer::renderAnsi(cb) << "\n";
                break;
            }

            case DisplayEventType::Error:
                spinnerRunning = false;
                out << "\n✕ " << event.text << "\n";
                break;

            case DisplayEventType::AnswerEnd:
                spinnerRunning = false;
                break;

            default:
                break;
        }
    }
};

// Helper to build DisplayEvents using static constructors
static DisplayEvent makeThinkingBlock(const std::string& text) {
    return DisplayEvent::thinkingBlock(text);
}

static DisplayEvent makeTextPartial(const std::string& text) {
    return DisplayEvent::textPartial(text);
}

static DisplayEvent makeTextParagraph(const std::string& text) {
    return DisplayEvent::textParagraph(text);
}

static DisplayEvent makeToolProgress(const std::string& tool, const std::string& activity) {
    return DisplayEvent::toolProgress("call_1", tool, activity);
}

static DisplayEvent makeToolResult(const std::string& tool, const std::string& summary) {
    return DisplayEvent::toolResult("call_1", tool, ToolResultSummary::success(summary));
}

static DisplayEvent makeAnswerStart() {
    return DisplayEvent::answerStart();
}

static DisplayEvent makeAnswerEnd() {
    return DisplayEvent::answerEnd();
}

// ============================================================
// Rendering tests
// ============================================================

TEST_CASE("Plain CLI: No repeated Thinking lines", "[PlainCli]") {
    PlainCliRenderer r;

    // Simulate: model thinks, then answers
    r.feed(makeThinkingBlock("Let me analyze..."));
    r.feed(makeThinkingBlock("I should check..."));
    r.feed(makeThinkingBlock("Looking at the code..."));

    std::string output = r.out.str();
    std::string stripped = stripAnsi(output);

    // ThinkingBlock events are suppressed — no "Thinking" in output
    CHECK(stripped.find("Thinking") == std::string::npos);
    // Output should be empty (no TextPartial yet)
    CHECK(output.find("Let me analyze") == std::string::npos);
}

TEST_CASE("Plain CLI: AnswerStart clears spinner line", "[PlainCli]") {
    PlainCliRenderer r;

    // AnswerStart → spinner stops, \n emitted
    r.feed(makeAnswerStart());
    r.feed(makeTextPartial("Hello world"));

    CHECK(r.spinnerRunning == false);
    // Output starts with \n (clears spinner line), then text
    CHECK(r.out.str().find("\nHello world") != std::string::npos);
}

TEST_CASE("Plain CLI: ToolResult does not interleave with AnswerText", "[PlainCli]") {
    PlainCliRenderer r;

    r.feed(makeAnswerStart());
    r.feed(makeTextPartial("Let me check the code."));
    r.feed(makeToolResult("Read", "Found 3 references"));
    r.feed(makeTextPartial("The references are at..."));
    r.feed(makeAnswerEnd());

    std::string output = r.out.str();
    std::string stripped = stripAnsi(output);

    // Verify ordering: AnswerText → ToolResult → AnswerText
    auto pos1 = stripped.find("Let me check the code.");
    auto pos2 = stripped.find("Found 3 references");
    auto pos3 = stripped.find("The references are at...");

    CHECK(pos1 != std::string::npos);
    CHECK(pos2 != std::string::npos);
    CHECK(pos3 != std::string::npos);
    CHECK(pos1 < pos2);
    CHECK(pos2 < pos3);

    // No "Thinking" spam
    CHECK(stripped.find("Thinking") == std::string::npos);
}

TEST_CASE("Plain CLI: ToolProgress suppressed in ANSI mode", "[PlainCli]") {
    PlainCliRenderer r;

    r.feed(makeAnswerStart());
    r.feed(makeToolProgress("Grep", "Searching for pattern..."));
    r.feed(makeTextPartial("Here's what I found."));
    r.feed(makeAnswerEnd());

    std::string stripped = stripAnsi(r.out.str());

    // ToolProgress activity text should NOT appear in output
    CHECK(stripped.find("Searching for pattern") == std::string::npos);
}

TEST_CASE("Plain CLI: Full streaming turn replay", "[PlainCli]") {
    // Simulate a complete turn: Thinking → tool use → final answer
    PlainCliRenderer r;

    // Phase 1: initial thinking (suppressed)
    r.feed(makeThinkingBlock("Let me find the references."));
    r.feed(makeThinkingBlock("I'll search the codebase."));

    // Phase 2: answer starts, tool execution
    r.feed(makeAnswerStart());
    r.feed(makeTextPartial("Let me search for handleDisplayEvent."));

    // Tool progress (suppressed)
    r.feed(makeToolProgress("Grep", "Searching handleDisplayEvent"));
    r.feed(makeToolResult("Grep", "Found 5 matches in 3 files"));

    // More text after tool
    r.feed(makeTextPartial("Now let me read the key files."));
    r.feed(makeToolResult("Read", "Read FtxuiRepl.cpp — 72 lines"));

    // Final answer
    r.feed(makeTextParagraph("The call chain is: FtxuiRepl.cpp:72 → StreamBuffer.cpp:45 → AnswerPostProcessor.cpp:89."));
    r.feed(makeAnswerEnd());

    std::string output = r.out.str();
    std::string stripped = stripAnsi(output);

    std::cout << "\n";
    std::cout << "=== Plain CLI Full Turn Replay Output ===\n";
    std::cout << output << "\n";
    std::cout << "=== End Output ===\n";

    // 1. No repeated "Thinking" lines
    size_t thinkingCount = 0;
    size_t pos = 0;
    while ((pos = stripped.find("Thinking", pos)) != std::string::npos) {
        thinkingCount++;
        pos++;
    }
    CHECK(thinkingCount == 0);

    // 2. AnswerText content is present and complete
    CHECK(stripped.find("handleDisplayEvent") != std::string::npos);
    CHECK(stripped.find("FtxuiRepl.cpp") != std::string::npos);

    // 3. Tool results are present
    CHECK(stripped.find("Found 5 matches") != std::string::npos);
    CHECK(stripped.find("Read FtxuiRepl.cpp") != std::string::npos);

    // 4. Order: first AnswerText → first ToolResult → second AnswerText → second ToolResult → final AnswerText
    auto posSearch = stripped.find("Let me search");
    auto posGrep = stripped.find("Found 5 matches");
    auto posNow = stripped.find("Now let me read");
    auto posRead = stripped.find("Read FtxuiRepl.cpp");
    auto posFinal = stripped.find("The call chain is");

    CHECK(posSearch != std::string::npos);
    CHECK(posGrep != std::string::npos);
    CHECK(posNow != std::string::npos);
    CHECK(posRead != std::string::npos);
    CHECK(posFinal != std::string::npos);

    CHECK(posSearch < posGrep);
    CHECK(posGrep < posNow);
    CHECK(posNow < posRead);
    CHECK(posRead < posFinal);

    // 5. Spinner is stopped by AnswerStart
    CHECK(r.spinnerRunning == false);
}

TEST_CASE("Plain CLI: TextPartial not duplicated or truncated", "[PlainCli]") {
    PlainCliRenderer r;

    r.feed(makeAnswerStart());
    r.feed(makeTextPartial("Part A "));
    r.feed(makeTextPartial("Part B "));
    r.feed(makeTextPartial("Part C"));
    r.feed(makeAnswerEnd());

    std::string stripped = stripAnsi(r.out.str());

    // All parts present in order
    CHECK(stripped.find("Part A") != std::string::npos);
    CHECK(stripped.find("Part B") != std::string::npos);
    CHECK(stripped.find("Part C") != std::string::npos);

    auto posA = stripped.find("Part A");
    auto posB = stripped.find("Part B");
    auto posC = stripped.find("Part C");
    CHECK(posA < posB);
    CHECK(posB < posC);

    // Each part appears exactly once
    CHECK(stripped.find("Part A", posA + 1) == std::string::npos);
    CHECK(stripped.find("Part B", posB + 1) == std::string::npos);
    CHECK(stripped.find("Part C", posC + 1) == std::string::npos);
}

TEST_CASE("Plain CLI: Multiple AnswerText blocks without ToolResult", "[PlainCli]") {
    // Pure text response (no tool calls)
    PlainCliRenderer r;

    r.feed(makeAnswerStart());
    r.feed(makeTextParagraph("Here is the analysis."));
    r.feed(makeTextParagraph("The code structure is clear."));
    r.feed(makeAnswerEnd());

    std::string stripped = stripAnsi(r.out.str());

    CHECK(stripped.find("Here is the analysis.") != std::string::npos);
    CHECK(stripped.find("The code structure is clear.") != std::string::npos);
    CHECK(stripped.find("Thinking") == std::string::npos);
}

TEST_CASE("Plain CLI: Error event stops spinner and renders", "[PlainCli]") {
    PlainCliRenderer r;

    r.feed(makeThinkingBlock("Let me try..."));
    r.feed(DisplayEvent::error("API connection failed"));

    CHECK(r.spinnerRunning == false);

    std::string stripped = stripAnsi(r.out.str());
    CHECK(stripped.find("API connection failed") != std::string::npos);
    CHECK(stripped.find("Thinking") == std::string::npos);
}
