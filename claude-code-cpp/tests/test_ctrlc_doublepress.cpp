/**
 * P3b: Ctrl+C double-press exit tests
 * Tests handleCtrlC() state machine directly — no FTXUI screen needed.
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <claude/ui/FtxuiRepl.hpp>
#include <chrono>

using namespace claude;
using namespace std::chrono_literals;

TEST_CASE("CtrlC double-press: first press does not exit", "[p3b][ctrlc]") {
    FtxuiRepl repl;
    bool cancelCalled = false;
    repl.setOnCancel([&]() { cancelCalled = true; });

    auto t0 = std::chrono::steady_clock::now();
    bool handled = repl.handleCtrlC(t0);

    CHECK(handled == true);
    CHECK(repl.isRunning() == true);
    CHECK(cancelCalled == false);

    // Verify hint block was added
    REQUIRE(!repl.contentBlocks().empty());
    CHECK(repl.contentBlocks().back().text == "Press Ctrl+C again to exit.");
    CHECK(repl.contentBlocks().back().dimmed == true);
}

TEST_CASE("CtrlC double-press: second press within timeout exits", "[p3b][ctrlc]") {
    FtxuiRepl repl;

    auto t0 = std::chrono::steady_clock::now();
    repl.handleCtrlC(t0);                    // first press
    CHECK(repl.isRunning() == true);

    auto t1 = t0 + 500ms;                    // within 2000ms
    bool handled = repl.handleCtrlC(t1);     // second press

    CHECK(handled == true);
    CHECK(repl.isRunning() == false);        // exit() was called
}

TEST_CASE("CtrlC double-press: timeout resets state", "[p3b][ctrlc]") {
    FtxuiRepl repl;

    auto t0 = std::chrono::steady_clock::now();
    repl.handleCtrlC(t0);                    // first press
    CHECK(repl.isRunning() == true);

    auto t1 = t0 + 2500ms;                   // past 2000ms timeout
    bool handled = repl.handleCtrlC(t1);     // should be treated as first press again
    CHECK(handled == true);
    CHECK(repl.isRunning() == true);         // still not exited

    // Verify only 2 hints total (one per press, but first is dedup'd?
    // Actually dedup is by "last block is hint" — since t0 and t1 both add
    // hints, and after t0 the last block IS the hint, so t1 will dedup.
    // So we should have exactly 1 hint.
    size_t hintCount = 0;
    for (auto& cb : repl.contentBlocks()) {
        if (cb.text == "Press Ctrl+C again to exit.") hintCount++;
    }
    CHECK(hintCount == 1);  // dedup prevented duplicate
}

TEST_CASE("CtrlC double-press: does not call onCancel", "[p3b][ctrlc]") {
    FtxuiRepl repl;
    bool cancelCalled = false;
    int cancelCount = 0;
    repl.setOnCancel([&]() { cancelCalled = true; cancelCount++; });

    auto t0 = std::chrono::steady_clock::now();
    repl.handleCtrlC(t0);                     // first press
    CHECK(cancelCalled == false);

    // Second press to exit — onCancel still not called
    auto t1 = t0 + 500ms;
    repl.handleCtrlC(t1);                     // second press exits
    CHECK(cancelCount == 0);                  // onCancel never fired
}

TEST_CASE("CtrlC double-press: custom timeout honored", "[p3b][ctrlc]") {
    FtxuiRepl repl;

    auto t0 = std::chrono::steady_clock::now();
    repl.handleCtrlC(t0, 500);               // 500ms timeout

    // Within custom timeout → exits
    auto t1 = t0 + 300ms;
    repl.handleCtrlC(t1, 500);
    CHECK(repl.isRunning() == false);
}

TEST_CASE("CtrlC double-press: duplicate hint suppressed on rapid repeat", "[p3b][ctrlc]") {
    FtxuiRepl repl;

    auto t0 = std::chrono::steady_clock::now();
    repl.handleCtrlC(t0);                     // adds hint

    // Wait past timeout, press again — should be first-press again
    // but hint dedup prevents adding a second hint block back-to-back
    auto t1 = t0 + 2500ms;
    repl.handleCtrlC(t1);                     // timeout expired, re-hint

    size_t hintCount = 0;
    for (auto& cb : repl.contentBlocks()) {
        if (cb.text == "Press Ctrl+C again to exit.") hintCount++;
    }
    CHECK(hintCount == 1);                    // dedup working
}
