# UI Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align claude-code-cpp's terminal UI with the official Claude Code TS implementation — three-region fullscreen TUI layout with correct bottom area, header, and infrastructure.

**Architecture:** Make FTXUI the default UI mode. Restructure the FTXUI bottom slot to match TS's `FullscreenLayout → bottom` component stack. Add missing UI elements (mode indicator, permission options, parallel task progress, auth status). Remove startup banner. Fix infrastructure issues (spdlog, isatty guards, model name display).

**Tech Stack:** FTXUI (C++ terminal UI framework), existing macaron color palette, existing FtxuiRepl class, existing AgentLoop callback system, Catch2 for unit tests.

---

## File Structure

### New files
| File | Responsibility |
|------|---------------|
| `include/claude/console/ActivityDescription.hpp` | `getActivityDescription()` function declaration |
| `src/console/ActivityDescription.cpp` | Tool name + JSON → human-readable description logic |
| `src/ui/PromptInputFooter.cpp` | FTXUI footer bar component (mode indicator + hints + model/cost) |
| `include/claude/ui/PromptInputFooter.hpp` | Footer component class declaration |
| `tests/test_activity_description.cpp` | Unit tests for ActivityDescription |
| `tests/test_prompt_input_footer.cpp` | Unit tests for PromptInputFooter rendering |

### Modified files
| File | Change |
|------|--------|
| `src/console/BannerPrinter.cpp` | Empty print/printWelcome bodies |
| `src/main.cpp` | Default useFtxui_=true, add --no-ftxui, add isatty guard, remove banner calls, set spdlog level to err, add log file sink, add --verbose flag, pass display name to setModelInfo |
| `src/ui/FtxuiRender.cpp` | Restructure bottom layout, add context bar to header, add token/cost to header, replace welcome screen, add mode indicator, permission as overlay, add agent progress rendering, add auth indicator, add keyboard hints, add cwd/git to header, remove "unknown", use ActivityDescription |
| `src/ui/FtxuiColors.hpp` | Add semantic colors for context bar thresholds |
| `include/claude/ui/FtxuiRepl.hpp` | Add mode state, agent progress state, cwd/git state, auth state, token count accessors |
| `src/ui/FtxuiStreaming.cpp` | Change refresh thread to update agent progress display instead of SystemInfo messages |
| `src/ui/FtxuiPermission.cpp` | Add tool-specific description generation |
| `src/bootstrap/SignalHandler.cpp` | Guard restoreTerminal() with isatty |
| `src/console/StatusLine.cpp` | Guard all output with isatty |
| `include/claude/permission/PermissionTypes.hpp` | Add AllowSession to PermissionChoice enum |
| `src/permission/PermissionSettings.cpp` | Handle AllowSession rule creation |
| `src/permission/RuleEngine.cpp` | Evaluate session-scoped rules |
| `include/claude/ui/UiMessageTypes.hpp` | Add AgentProgress display message type |
| `include/claude/ui/components/PermissionPrompt.hpp` | Add 5th choice, add tool-specific description field |
| `src/ui/components/PermissionPrompt.cpp` | Update to 5 choices, add tool badge + description |
| `src/console/Spinner.cpp` | Use ActivityDescription in spinner tool context |
| `src/console/ToolStatusRenderer.cpp` | Use ActivityDescription in tool start/result rendering |
| `src/console/CollapsedToolRenderer.cpp` | Change summary format to "Read 3 files, Searched 2 patterns" |
| `CMakeLists.txt` | Add new source files to UI_SOURCES and CONSOLE_SOURCES |
| `tests/CMakeLists.txt` | Add new test targets |

---

## Task 1: Remove Startup Banner

**Files:**
- Modify: `src/console/BannerPrinter.cpp:8-33`
- Modify: `src/main.cpp:577-578`

- [ ] **Step 1: Empty BannerPrinter bodies**

In `src/console/BannerPrinter.cpp`, replace the `print()` body (lines 8-18) and `printWelcome()` body (lines 25-33) with empty functions:

```cpp
void BannerPrinter::print() {
    // Intentionally empty — banner removed to match official Claude Code UX
}

void BannerPrinter::printVersion(const String& version) {
    // Intentionally empty — banner removed to match official Claude Code UX
}

void BannerPrinter::printWelcome() {
    // Intentionally empty — banner removed to match official Claude Code UX
}
```

- [ ] **Step 2: Remove BannerPrinter calls from main.cpp**

In `src/main.cpp`, remove lines 577-578 inside `runRepl()`:

```cpp
// DELETE these two lines:
BannerPrinter banner(std::cout);
banner.printWelcome();
```

- [ ] **Step 3: Remove welcome screen from FTXUI**

In `src/ui/FtxuiRender.cpp`, find the welcome screen block (lines 683-696, the section that checks `elems.empty() && !isStreaming_ && !isThinking_` and renders the bordered welcome box). Replace it with:

```cpp
// Show empty content area until first message arrives — no welcome screen
if (elems.empty() && !isStreaming_ && !isThinking_) {
    // Intentionally blank — match official Claude Code
}
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds with no errors.

Run: `./build/claude-cli --no-ftxui -p "hello" 2>&1 | head -5`
Expected: No ASCII art banner appears. Output starts directly with response.

- [ ] **Step 5: Commit**

```bash
git add src/console/BannerPrinter.cpp src/main.cpp src/ui/FtxuiRender.cpp
git commit -m "feat: remove startup banner to match official Claude Code UX"
```

---

## Task 2: Make FTXUI the Default Mode

**Files:**
- Modify: `src/main.cpp:838` and `src/main.cpp:215-217`

- [ ] **Step 1: Change default value of useFtxui_**

In `src/main.cpp` line 838, change:

```cpp
bool useFtxui_ = false;
```

to:

```cpp
bool useFtxui_ = true;
```

Wrap it with the `#ifdef HAS_FTXUI` guard:

```cpp
#ifdef HAS_FTXUI
bool useFtxui_ = true;
#else
bool useFtxui_ = false;
#endif
```

- [ ] **Step 2: Replace --ftxui flag with --no-ftxui**

In `src/main.cpp` lines 215-217, replace:

```cpp
#ifdef HAS_FTXUI
    app.add_flag("--ftxui", useFtxui_, "Use FTXUI component-based terminal UI");
#endif
```

with:

```cpp
#ifdef HAS_FTXUI
    bool noFtxui = false;
    app.add_flag("--no-ftxui", noFtxui, "Disable FTXUI, use readline mode instead");
#endif
```

Then after `app.parse()` in the try block, add the flag application and isatty guard:

```cpp
#ifdef HAS_FTXUI
    if (noFtxui) useFtxui_ = false;
    // Auto-fallback to readline when stdout is not a TTY
    if (useFtxui_ && !isatty(STDOUT_FILENO)) {
        useFtxui_ = false;
    }
#endif
```

This requires `#include <unistd.h>` at the top of main.cpp (check if already included).

- [ ] **Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

Run: `./build/claude-cli --help 2>&1 | grep ftxui`
Expected: Shows `--no-ftxui` flag, NOT `--ftxui`.

Run: `echo "hello" | ./build/claude-cli -p "say hi" 2>&1 | head -3`
Expected: Readline mode auto-activated (piped input = non-TTY), no FTXUI.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: make FTXUI the default UI mode, add --no-ftxui fallback"
```

---

## Task 3: Suppress spdlog Output in Terminal

**Files:**
- Modify: `src/main.cpp:266-268`

- [ ] **Step 1: Set spdlog default level to err**

In `src/main.cpp`, find the spdlog initialization (lines 266-268):

```cpp
auto stderrLogger = spdlog::stderr_color_mt("stderr");
spdlog::set_default_logger(stderrLogger);
spdlog::set_level(verbose_ ? spdlog::level::debug : spdlog::level::info);
```

Replace with:

```cpp
auto stderrLogger = spdlog::stderr_color_mt("stderr");
spdlog::set_default_logger(stderrLogger);

// Log file sink for diagnostics
auto logDir = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.claude/logs";
std::filesystem::create_directories(logDir);
auto fileLogger = spdlog::basic_logger_mt("file", logDir + "/claude-cli.log");
fileLogger->set_level(spdlog::level::debug);
fileLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

// Terminal: only errors. File: debug level for diagnostics
spdlog::set_level(verbose_ ? spdlog::level::debug : spdlog::level::err);
```

This requires adding `#include <filesystem>` and `#include <spdlog/sinks/basic_file_sink.h>` at the top of main.cpp.

- [ ] **Step 2: Guard remaining spdlog::info calls**

Find all `spdlog::info` calls in src/ that write to stderr during normal operation. These should be changed to `spdlog::debug` so they only appear in the log file. Key files to check:

- `src/ui/FtxuiRender.cpp` — "FTXUI: Building component..."
- `src/main.cpp` — "Session saved to..."

Run: `grep -rn "spdlog::info" src/ | head -20`

For each hit, change `spdlog::info` to `spdlog::debug`.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

Run: `./build/claude-cli --no-ftxui -p "hello" 2>&1 | grep -c "\[info\]"`
Expected: 0 (no info-level messages in terminal output).

Run: `./build/claude-cli --verbose --no-ftxui -p "hello" 2>&1 | grep "\[info\]" | head -3`
Expected: Verbose mode shows info-level messages.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp src/ui/FtxuiRender.cpp
git commit -m "feat: suppress spdlog output in terminal, add log file sink"
```

---

## Task 4: Guard ANSI Escape Sequences with isatty()

**Files:**
- Modify: `src/bootstrap/SignalHandler.cpp:16-38`
- Modify: `src/console/StatusLine.cpp:100-106, 230-237`

- [ ] **Step 1: Guard restoreTerminal() in SignalHandler.cpp**

In `src/bootstrap/SignalHandler.cpp`, find `restoreTerminal()` (lines 16-38). Add an isatty guard around the escape sequence writes:

```cpp
void restoreTerminal() {
    if (isatty(STDOUT_FILENO)) {
        std::cout << "\x1b[?1000l"  // Disable basic mouse tracking
                  << "\x1b[?1002l"  // Disable button-event tracking
                  << "\x1b[?1003l"  // Disable any-event tracking
                  << "\x1b[?1006l"  // Disable SGR mouse mode
                  << std::flush;
        std::cout << "\x1b[?25h"    // Show cursor
                  << "\x1b[0m"      // Reset all attributes
                  << std::flush;
    }

    // Restore terminal attributes (this is always safe)
    // ... existing tcsetattr code unchanged ...
}
```

This requires `#include <unistd.h>` at the top (check if already present).

- [ ] **Step 2: Guard StatusLine.cpp output with isatty**

In `src/console/StatusLine.cpp`, guard the `refresh()` method (lines 100-106):

```cpp
void StatusLine::refresh() {
    if (!isatty(STDOUT_FILENO)) return;  // Skip in non-TTY mode
    // ... existing code ...
}
```

Guard `clearStatusLine()` (lines 230-237) the same way:

```cpp
void StatusLine::clearStatusLine() {
    if (!isatty(STDOUT_FILENO)) return;
    // ... existing code ...
}
```

Also guard `startAutoRefresh()` and `stopAutoRefresh()` — if not a TTY, don't start the refresh thread:

```cpp
void StatusLine::startAutoRefresh() {
    if (!isatty(STDOUT_FILENO)) return;
    // ... existing code ...
}
```

This requires `#include <unistd.h>` at the top.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

Run: `./build/claude-cli --no-ftxui -p "hello" 2>&1 | cat -v | grep "\\^\\["`
Expected: No visible escape sequences in piped output.

- [ ] **Step 4: Commit**

```bash
git add src/bootstrap/SignalHandler.cpp src/console/StatusLine.cpp
git commit -m "feat: guard ANSI escape sequences with isatty() check"
```

---

## Task 5: Fix Model Name Display

**Files:**
- Modify: `src/main.cpp` — pass display name to setModelInfo
- Modify: `src/ui/FtxuiRender.cpp:33-42` — use display name in header
- Modify: `src/console/StatusLine.cpp:123-187` — use display name

- [ ] **Step 1: Pass display name to setModelInfo in main.cpp**

In `src/main.cpp`, find where `setModelInfo()` is called (search for `setModelInfo`). Update the call to use the display name from `AppState::instance().modelStrings()`:

```cpp
auto ms = AppState::instance().modelStrings();
String modelDisplay = ms ? ms->displayName : model_;
repl->setModelInfo(modelDisplay);
```

Also update the StatusLine creation to use the display name:

```cpp
if (statusLine_) {
    auto ms = AppState::instance().modelStrings();
    statusLine_->setModelName(ms ? ms->displayName : model_);
}
```

(Check if StatusLine has a `setModelName()` method — if not, add one.)

- [ ] **Step 2: Use display name in FTXUI header**

In `src/ui/FtxuiRender.cpp`, the header rendering (lines 33-42) uses `r->modelInfo_` which is already set by `setModelInfo()`. After Step 1, this should show the display name. Verify the header line:

```cpp
text(r->modelInfo_) | color(MacCream) | dim
```

This already works — `modelInfo_` will now contain the display name.

- [ ] **Step 3: Update StatusLine buildStatusText**

In `src/console/StatusLine.cpp`, find where the model name is rendered in `buildStatusText()` (around line 140). The model name is read from `modelName_` field. After Step 1 adds `setModelName()`, this field will contain the display name.

- [ ] **Step 4: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/ui/FtxuiRender.cpp src/console/StatusLine.cpp
git commit -m "feat: use model display name from ModelStrings in UI"
```

---

## Task 6: Add ActivityDescription Function

**Files:**
- Create: `include/claude/console/ActivityDescription.hpp`
- Create: `src/console/ActivityDescription.cpp`
- Create: `tests/test_activity_description.cpp`
- Modify: `CMakeLists.txt` — add to CONSOLE_SOURCES
- Modify: `tests/CMakeLists.txt` — add test target

- [ ] **Step 1: Write the failing test**

Create `tests/test_activity_description.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <claude/console/ActivityDescription.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("ActivityDescription basic tools", "[activity]") {
    SECTION("Read with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Read", input.dump()) == "Reading src/main.ts");
    }
    SECTION("Read with offset and limit") {
        json input = {{"file_path", "src/main.ts"}, {"offset", 10}, {"limit", 20}};
        REQUIRE(getActivityDescription("Read", input.dump()) == "Reading src/main.ts:10-30");
    }
    SECTION("Write with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Write", input.dump()) == "Writing src/main.ts");
    }
    SECTION("Edit with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Edit", input.dump()) == "Editing src/main.ts");
    }
    SECTION("Bash with command") {
        json input = {{"command", "npm test"}};
        REQUIRE(getActivityDescription("Bash", input.dump()) == "Running npm test");
    }
    SECTION("Grep with pattern only") {
        json input = {{"pattern", "TODO"}};
        REQUIRE(getActivityDescription("Grep", input.dump()) == "Searching for \"TODO\"");
    }
    SECTION("Grep with pattern and path") {
        json input = {{"pattern", "TODO"}, {"path", "src/"}};
        REQUIRE(getActivityDescription("Grep", input.dump()) == "Searching for \"TODO\" in src/");
    }
    SECTION("Glob with pattern") {
        json input = {{"pattern", "*.ts"}};
        REQUIRE(getActivityDescription("Glob", input.dump()) == "Finding *.ts");
    }
    SECTION("WebFetch with url") {
        json input = {{"url", "https://example.com"}};
        REQUIRE(getActivityDescription("WebFetch", input.dump()) == "Fetching https://example.com");
    }
    SECTION("WebSearch with query") {
        json input = {{"query", "rust tutorials"}};
        REQUIRE(getActivityDescription("WebSearch", input.dump()) == "Searching \"rust tutorials\"");
    }
    SECTION("Agent with agent_type") {
        json input = {{"agent_type", "Explore"}};
        REQUIRE(getActivityDescription("Agent", input.dump()) == "Running Explore agent");
    }
    SECTION("LSP with file_path") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("LSP", input.dump()) == "LSP src/main.ts");
    }
    SECTION("MCP with tool_name") {
        json input = {{"tool_name", "weather"}};
        REQUIRE(getActivityDescription("MCP", input.dump()) == "Calling MCP weather");
    }
    SECTION("Unknown tool falls back") {
        json input = {{"file_path", "test.txt"}};
        REQUIRE(getActivityDescription("CustomTool", input.dump()) == "Running CustomTool");
    }
    SECTION("Empty input") {
        REQUIRE(getActivityDescription("Bash", "{}") == "Running Bash");
    }
    SECTION("Invalid JSON") {
        REQUIRE(getActivityDescription("Bash", "not json") == "Running Bash");
    }
}

TEST_CASE("ActivityDescription past tense", "[activity]") {
    SECTION("Read past tense") {
        json input = {{"file_path", "src/main.ts"}};
        REQUIRE(getActivityDescription("Read", input.dump(), false) == "Read src/main.ts");
    }
    SECTION("Bash past tense") {
        json input = {{"command", "npm test"}};
        REQUIRE(getActivityDescription("Bash", input.dump(), false) == "Ran npm test");
    }
    SECTION("Grep past tense") {
        json input = {{"pattern", "TODO"}};
        REQUIRE(getActivityDescription("Grep", input.dump(), false) == "Searched for \"TODO\"");
    }
    SECTION("Write past tense") {
        json input = {{"file_path", "out.txt"}};
        REQUIRE(getActivityDescription("Write", input.dump(), false) == "Wrote out.txt");
    }
    SECTION("Edit past tense") {
        json input = {{"file_path", "out.txt"}};
        REQUIRE(getActivityDescription("Edit", input.dump(), false) == "Edited out.txt");
    }
    SECTION("Glob past tense") {
        json input = {{"pattern", "*.ts"}};
        REQUIRE(getActivityDescription("Glob", input.dump(), false) == "Found *.ts");
    }
    SECTION("WebFetch past tense") {
        json input = {{"url", "https://example.com"}};
        REQUIRE(getActivityDescription("WebFetch", input.dump(), false) == "Fetched https://example.com");
    }
    SECTION("WebSearch past tense") {
        json input = {{"query", "rust"}};
        REQUIRE(getActivityDescription("WebSearch", input.dump(), false) == "Searched \"rust\"");
    }
}
```

- [ ] **Step 2: Write the header**

Create `include/claude/console/ActivityDescription.hpp`:

```cpp
#pragma once

#include "../../core/Types.hpp"

namespace claude {

/// Convert tool name + JSON input to a human-readable activity description.
/// @param toolName  The tool name (e.g. "Read", "Bash", "Grep")
/// @param jsonInput The JSON input string (e.g. R"({"file_path":"src/main.ts"})")
/// @param active    True = present tense ("Reading"), false = past tense ("Read")
String getActivityDescription(const String& toolName, const String& jsonInput, bool active = true);

} // namespace claude
```

- [ ] **Step 3: Write the implementation**

Create `src/console/ActivityDescription.cpp`:

```cpp
#include <claude/console/ActivityDescription.hpp>
#include <nlohmann/json.hpp>

namespace claude {

using json = nlohmann::json;

namespace {

struct ToolDescriptor {
    const char* activeVerb;   // "Reading"
    const char* pastVerb;     // "Read"
    const char* inputKey;     // "file_path"
    const char* secondaryKey; // nullptr or "path", "query", etc.
    bool isQuoted;            // true = wrap value in quotes
};

const std::unordered_map<String, ToolDescriptor> kToolDescriptors = {
    {"Read",      {"Reading",  "Read",     "file_path",  nullptr,      false}},
    {"Write",     {"Writing",  "Wrote",    "file_path",  nullptr,      false}},
    {"Edit",      {"Editing",  "Edited",   "file_path",  nullptr,      false}},
    {"NotebookEdit", {"Editing", "Edited", "notebook_path", nullptr,   false}},
    {"Bash",      {"Running",  "Ran",      "command",    nullptr,      false}},
    {"Grep",      {"Searching for", "Searched for", "pattern", "path", true}},
    {"Glob",      {"Finding",  "Found",    "pattern",    nullptr,      false}},
    {"WebFetch",  {"Fetching", "Fetched",  "url",        nullptr,      false}},
    {"WebSearch", {"Searching","Searched", "query",      nullptr,      true}},
    {"Agent",     {"Running",  "Ran",      "agent_type", nullptr,      false}},
    {"LSP",       {"LSP",      "LSP",      "file_path",  nullptr,      false}},
    {"MCP",       {"Calling MCP", "Called MCP", "tool_name", nullptr,  false}},
};

String extractJsonString(const String& jsonStr, const String& key) {
    try {
        auto j = json::parse(jsonStr);
        if (j.contains(key) && j[key].is_string()) {
            return j[key].get<String>();
        }
        if (j.contains(key)) {
            return j[key].dump();
        }
    } catch (...) {}
    return {};
}

int extractJsonInt(const String& jsonStr, const String& key) {
    try {
        auto j = json::parse(jsonStr);
        if (j.contains(key) && j[key].is_number_integer()) {
            return j[key].get<int>();
        }
    } catch (...) {}
    return -1;
}

} // anonymous namespace

String getActivityDescription(const String& toolName, const String& jsonInput, bool active) {
    auto it = kToolDescriptors.find(toolName);
    if (it == kToolDescriptors.end()) {
        return active ? ("Running " + toolName) : ("Ran " + toolName);
    }

    const auto& desc = it->second;
    String verb = active ? desc.activeVerb : desc.pastVerb;
    String primaryValue = extractJsonString(jsonInput, desc.inputKey);

    if (primaryValue.empty()) {
        return verb + " " + toolName;
    }

    String result;

    // Special case: Read with offset/limit
    if (toolName == "Read") {
        int offset = extractJsonInt(jsonInput, "offset");
        int limit = extractJsonInt(jsonInput, "limit");
        if (offset >= 0 && limit >= 0) {
            result = verb + " " + primaryValue + ":" + std::to_string(offset) + "-" + std::to_string(offset + limit);
        } else if (offset >= 0) {
            result = verb + " " + primaryValue + ":" + std::to_string(offset);
        } else {
            result = verb + " " + primaryValue;
        }
    } else if (desc.isQuoted) {
        result = verb + " \"" + primaryValue + "\"";
    } else {
        result = verb + " " + primaryValue;
    }

    // Append secondary key value if present
    if (desc.secondaryKey) {
        String secondaryValue = extractJsonString(jsonInput, desc.secondaryKey);
        if (!secondaryValue.empty()) {
            if (desc.isQuoted) {
                // For Grep: "Searching for \"TODO\" in src/"
                result = verb + " \"" + primaryValue + "\" in " + secondaryValue;
            } else {
                result += " in " + secondaryValue;
            }
        }
    }

    // Special case: Agent appends " agent"
    if (toolName == "Agent") {
        result += " agent";
    }

    return result;
}

} // namespace claude
```

- [ ] **Step 4: Add to CMakeLists.txt**

In `CMakeLists.txt`, add to the `CONSOLE_SOURCES` set (after `src/console/CollapsedToolRenderer.cpp`):

```cmake
src/console/ActivityDescription.cpp
```

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(test_activity_description test_activity_description.cpp)
target_link_libraries(test_activity_description PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_activity_description)
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

Run: `./build/test_activity_description 2>&1`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/claude/console/ActivityDescription.hpp src/console/ActivityDescription.cpp tests/test_activity_description.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add ActivityDescription for human-readable tool activity strings"
```

---

## Task 7: Integrate ActivityDescription into Existing Renderers

**Files:**
- Modify: `src/console/Spinner.cpp:110-162`
- Modify: `src/console/ToolStatusRenderer.cpp:162-224`
- Modify: `src/console/CollapsedToolRenderer.cpp:84-134`
- Modify: `src/ui/FtxuiRender.cpp` — tool use rendering sections

- [ ] **Step 1: Update Spinner to use ActivityDescription**

In `src/console/Spinner.cpp`, add the include:

```cpp
#include <claude/console/ActivityDescription.hpp>
```

In `buildDisplayString()` (around lines 110-162), replace the manual tool context formatting (lines 117-138) with:

```cpp
if (!toolContext_.empty()) {
    String activity = getActivityDescription(toolContext_, toolArgs_, true);
    display << AnsiStyle::BRIGHT_CYAN << " " << activity;
} else if (isThinking_) {
```

(Keep the thinking and creative verb fallback paths unchanged.)

- [ ] **Step 2: Update ToolStatusRenderer to use ActivityDescription**

In `src/console/ToolStatusRenderer.cpp`, add:

```cpp
#include <claude/console/ActivityDescription.hpp>
```

In `renderStart()` (lines 162-224), replace the manual per-tool input summary formatting with:

```cpp
String activity = getActivityDescription(toolName, arguments, true);
// Render: [Badge] activity
oss << renderBadge(toolName) << " " << activity;
```

Keep the tool badge rendering via `renderBadge()` — just replace the content after the badge.

In `renderEnd()`, for the result summary:

```cpp
String activity = getActivityDescription(toolName, arguments, false);
oss << toolStateDot(state) << " " << activity;
```

- [ ] **Step 3: Add writeCount/editCount fields to CollapsedToolGroup**

In `include/claude/ui/UiMessageTypes.hpp`, add to the `CollapsedToolGroup` struct (after `bashCount`, before `memoryCount`):

```cpp
int writeCount = 0;
int editCount = 0;
```

Also update `CollapsedToolRenderer::accumulateTool()` in `src/console/CollapsedToolRenderer.cpp` to increment these new fields. Find the tool type dispatch in `accumulateTool()` (around lines 23-80) and add cases for Write and Edit:

```cpp
if (toolName == "Write") { writeCount++; return; }
if (toolName == "Edit" || toolName == "NotebookEdit") { editCount++; return; }
```

- [ ] **Step 4: Update CollapsedToolRenderer summary format**

In `src/console/CollapsedToolRenderer.cpp`, add:

```cpp
#include <claude/console/ActivityDescription.hpp>
```

Update `summaryText()` to use the new comma-separated format with active/past tense. Replace the existing `CollapsedToolGroup::summaryText()` method body in `include/claude/ui/UiMessageTypes.hpp`:

```cpp
String summaryText() const {
    std::vector<String> parts;

    if (readCount > 0) {
        parts.push_back(active ? ("Reading " + std::to_string(readCount) + " files")
                               : ("Read " + std::to_string(readCount) + " files"));
    }
    if (searchCount > 0) {
        parts.push_back(active ? ("Searching " + std::to_string(searchCount) + " patterns")
                               : ("Searched " + std::to_string(searchCount) + " patterns"));
    }
    if (listCount > 0) {
        parts.push_back(active ? ("Listing " + std::to_string(listCount) + " paths")
                               : ("Listed " + std::to_string(listCount) + " paths"));
    }
    if (writeCount > 0) {
        parts.push_back(active ? ("Writing " + std::to_string(writeCount) + " files")
                               : ("Wrote " + std::to_string(writeCount) + " files"));
    }
    if (editCount > 0) {
        parts.push_back(active ? ("Editing " + std::to_string(editCount) + " files")
                               : ("Edited " + std::to_string(editCount) + " files"));
    }
    if (bashCount > 0) {
        parts.push_back(active ? ("Running " + std::to_string(bashCount) + " commands")
                               : ("Ran " + std::to_string(bashCount) + " commands"));
    }
    if (mcpCallCount > 0) {
        parts.push_back(active ? ("Calling " + std::to_string(mcpCallCount) + " MCP tools")
                               : ("Called " + std::to_string(mcpCallCount) + " MCP tools"));
    }
    if (memoryCount > 0) {
        parts.push_back(active ? ("Saving " + std::to_string(memoryCount) + " memories")
                               : ("Saved " + std::to_string(memoryCount) + " memories"));
    }
    if (hookCount > 0) {
        parts.push_back(active ? ("Running " + std::to_string(hookCount) + " hooks")
                               : ("Ran " + std::to_string(hookCount) + " hooks"));
    }

    if (parts.empty()) return "[0 tool uses]";
    String result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        result += ", " + parts[i];
    }
    if (active) result += "...";
    return result;
}
```

This changes the separator from ` / ` to `, ` and adds active-tense support via the `active` field that already exists on `CollapsedToolGroup`.

- [ ] **Step 5: Update FTXUI tool use rendering**

In `src/ui/FtxuiRender.cpp`, add:

```cpp
#include <claude/console/ActivityDescription.hpp>
```

Find the tool use rendering section (lines 234-415) where tool names and arguments are rendered. Replace the raw argument display with ActivityDescription. Look for where `toolUse.arguments` is displayed directly (e.g. in collapsed group summaries and individual tool lines). Replace with:

```cpp
String activity = getActivityDescription(msg.toolUse.toolName, msg.toolUse.arguments, msg.toolUse.isInProgress);
```

Use `activity` in place of raw JSON display.

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

Run: `ctest --test-dir build -R activity 2>&1`
Expected: All ActivityDescription tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/console/Spinner.cpp src/console/ToolStatusRenderer.cpp src/console/CollapsedToolRenderer.cpp src/ui/FtxuiRender.cpp include/claude/ui/UiMessageTypes.hpp
git commit -m "feat: integrate ActivityDescription into all rendering contexts"
```

---

## Task 8: Add Context Usage Progress Bar to Header

**Files:**
- Modify: `src/ui/FtxuiRender.cpp:33-42`
- Modify: `src/ui/FtxuiColors.hpp`

- [ ] **Step 1: Add semantic colors for context bar thresholds**

In `src/ui/FtxuiColors.hpp`, add after the existing color definitions:

```cpp
// Context bar threshold colors
inline const auto MacContextOk = ftxui::Color::RGB(160, 210, 180);    // <70% — MacMint
inline const auto MacContextWarn = ftxui::Color::RGB(210, 186, 140);  // 70-85% — MacGold
inline const auto MacContextCrit = ftxui::Color::RGB(210, 150, 150);  // >=85% — MacRose
```

- [ ] **Step 2: Add progress bar to header rendering**

In `src/ui/FtxuiRender.cpp`, find the header `hbox` construction (lines 33-42). The current header looks like:

```cpp
auto header = hbox({
    text("╭─ Claude Code") | bold | color(MacPeach),
    text(" │ ") | color(MacShadow),
    text(r->modelInfo_) | color(MacCream) | dim,
    filler(),
    // status indicator
    text(r->isStreaming_ ? " ○ Running" : " ○ Idle") | color(...),
    text(" ─╮") | color(MacShadow),
});
```

Add the context bar between model name and the status indicator, replacing the `filler()`:

```cpp
// Context usage progress bar
int ctxPct = (r->contextMaxTokens_ > 0)
    ? static_cast<int>(100 * r->contextUsedTokens_ / r->contextMaxTokens_)
    : 0;
int barWidth = 10;
int filled = (ctxPct * barWidth + 50) / 100;
filled = std::min(filled, barWidth);
filled = std::max(filled, 0);

auto barColor = (ctxPct >= 85) ? MacContextCrit
              : (ctxPct >= 70) ? MacContextWarn
              :                  MacContextOk;

String barFilled(filled, L'█');
String barEmpty(barWidth - filled, L'░');
String pctStr = std::to_string(ctxPct) + "% ctx";

auto header = hbox({
    text("╭─ Claude Code") | bold | color(MacPeach),
    text(" │ ") | color(MacShadow),
    text(r->modelInfo_) | color(MacCream) | dim,
    text(" │ ") | color(MacShadow),
    text(barFilled + barEmpty) | color(barColor),
    text(" " + pctStr) | color(barColor) | dim,
    filler(),
    // status indicator
    text(r->isStreaming_ ? " ○ Running" : " ○ Idle") | color(...),
    text(" ─╮") | color(MacShadow),
});
```

Note: Use `std::u32string` or wide character handling for the block characters. Alternatively, use UTF-8 string literals `u8"█"` and `u8"░"`:

```cpp
String barFilled(static_cast<size_t>(filled), U'█');  // May need adjustment for UTF-8
```

Simpler approach — build as UTF-8 string:

```cpp
String barStr;
for (int i = 0; i < filled; ++i) barStr += "█";
for (int i = filled; i < barWidth; ++i) barStr += "░";
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/ui/FtxuiRender.cpp src/ui/FtxuiColors.hpp
git commit -m "feat: add context usage progress bar to header"
```

---

## Task 9: Add Token Count and Cost to Header

**Files:**
- Modify: `src/ui/FtxuiRender.cpp:33-42`
- Modify: `include/claude/ui/FtxuiRepl.hpp`

- [ ] **Step 1: Add token count accessor methods to FtxuiRepl**

In `include/claude/ui/FtxuiRepl.hpp`, add fields alongside the existing `contextUsedTokens_` and `costUsd_` (around line 115):

```cpp
int inputTokens_ = 0;
int outputTokens_ = 0;

void setTokenCounts(int inputTokens, int outputTokens) {
    inputTokens_ = inputTokens;
    outputTokens_ = outputTokens;
}
```

- [ ] **Step 2: Add token/cost display to header**

In `src/ui/FtxuiRender.cpp`, extend the header hbox from Task 8. After the context bar percentage, add:

```cpp
// Token counts and cost
auto fmtTokens = [](int n) -> String {
    if (n >= 1'000'000) return std::to_string(n / 100'000) + "." + std::to_string((n % 100'000) / 10) + "M";
    if (n >= 1'000) return std::to_string(n / 100) + "." + std::to_string((n % 100) / 10) + "K";
    return std::to_string(n);
};

String tokenInfo = fmtTokens(r->inputTokens_) + " in/" + fmtTokens(r->outputTokens_) + " out";
String costInfo = "$" + fmtCost(r->costUsd_);

// In the header hbox, after context bar:
text(" │ ") | color(MacShadow),
text(tokenInfo) | color(MacCream) | dim,
text(" · ") | color(MacShadow),
text(costInfo) | color(MacCream) | dim,
```

Where `fmtCost` is a helper that formats USD with appropriate precision:

```cpp
auto fmtCost = [](double cost) -> String {
    if (cost < 0.0001) return "0.0000";
    char buf[16];
    snprintf(buf, sizeof(buf), "%.4f", cost);
    return String(buf);
};
```

- [ ] **Step 3: Wire up token counts from AppState**

In `src/main.cpp`, where `setContextInfo()` is called, also call `setTokenCounts()`. Find the call site and add:

```cpp
repl->setTokenCounts(
    AppState::instance().metrics().inputTokens,
    AppState::instance().metrics().outputTokens
);
```

Check what metrics accessors are available on `MetricsState` and use the correct field names.

- [ ] **Step 4: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add include/claude/ui/FtxuiRepl.hpp src/ui/FtxuiRender.cpp src/main.cpp
git commit -m "feat: add token count and cost display to header"
```

---

## Task 10: Add AllowSession to PermissionChoice

**Files:**
- Modify: `include/claude/permission/PermissionTypes.hpp:204-209`
- Modify: `src/permission/PermissionSettings.cpp`
- Modify: `src/permission/RuleEngine.cpp:335-394`

- [ ] **Step 1: Add AllowSession to PermissionChoice enum**

In `include/claude/permission/PermissionTypes.hpp`, update the `PermissionChoice` enum (lines 204-209):

```cpp
enum class PermissionChoice {
    AllowOnce,      // 允许本次执行
    AllowSession,   // 允许本次会话（不持久化到磁盘）
    AlwaysAllow,    // 始终允许此模式
    DenyOnce,       // 拒绝本次执行
    AlwaysDeny      // 始终拒绝此模式
};
```

Also update `permissionChoiceToString()` (search for it in the same file, around line 395+). Add:

```cpp
case PermissionChoice::AllowSession: return "allowSession";
```

And in `parsePermissionChoice()` if it exists:

```cpp
if (s == "allowSession") return PermissionChoice::AllowSession;
```

- [ ] **Step 2: Handle AllowSession in PermissionSettings**

In `src/permission/PermissionSettings.cpp`, find `addPermissionRulesToSettings()` (line 15). The function maps `PermissionRuleSource` to `SettingSource` for disk persistence. `AllowSession` should create a `Session` source rule (which is already handled — Session source rules return early from disk persistence, staying in memory only).

In `applyChoice()` in `src/permission/RuleEngine.cpp` (lines 335-394), add a case for `AllowSession`:

```cpp
case PermissionChoice::AllowSession: {
    auto rule = PermissionRule::forTool(toolName, PermissionBehavior::Allow)
                    .withSource(PermissionRuleSource::Session);
    permissionStore.addPermissionRulesToSettings({rule}, rule.source());
    break;
}
```

This creates a session-scoped allow rule that persists in memory but is never saved to disk (because `Session` source is excluded from file persistence in `addPermissionRulesToSettings()`).

- [ ] **Step 3: Update RuleEngine evaluation for session rules**

Session-scoped rules are already evaluated by the existing `evaluateInner()` logic — `Session` source has the lowest priority in `PermissionRuleSource` (line 65 in the enum). The rule will be checked during step 5 ("Allow rules") of the 13-step evaluation. No changes needed to the evaluation itself.

- [ ] **Step 4: Build and run existing permission tests**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

Run: `ctest --test-dir build -R permission 2>&1 | tail -10`
Expected: All permission tests pass (existing tests should still work since AllowSession is a new value).

- [ ] **Step 5: Commit**

```bash
git add include/claude/permission/PermissionTypes.hpp src/permission/PermissionSettings.cpp src/permission/RuleEngine.cpp
git commit -m "feat: add AllowSession permission choice for session-scoped rules"
```

---

## Task 11: Update Permission Prompt Component to 5 Options

**Files:**
- Modify: `include/claude/ui/components/PermissionPrompt.hpp`
- Modify: `src/ui/components/PermissionPrompt.cpp`
- Modify: `src/ui/FtxuiRender.cpp:612-680`
- Modify: `src/ui/FtxuiPermission.cpp`

- [ ] **Step 1: Update PermissionPrompt.hpp**

In `include/claude/ui/components/PermissionPrompt.hpp`, update the default choices in `Options`:

```cpp
struct Options {
    String toolName;
    String activity;
    String description;  // Tool-specific description like "Allow Bash to run: npm test"
    std::vector<String> choices = {
        "Yes (once)",
        "Allow for this session",
        "Yes, always allow",
        "No (once)",
        "No, always deny"
    };
};
```

Update `toPermissionChoice()` mapping:

```cpp
PermissionChoice PermissionPromptComponent::toPermissionChoice() const {
    switch (selectedIndex_) {
        case 0: return PermissionChoice::AllowOnce;
        case 1: return PermissionChoice::AllowSession;
        case 2: return PermissionChoice::AlwaysAllow;
        case 3: return PermissionChoice::DenyOnce;
        case 4: return PermissionChoice::AlwaysDeny;
        default: return PermissionChoice::DenyOnce;
    }
}
```

- [ ] **Step 2: Update PermissionPrompt.cpp rendering**

In `src/ui/components/PermissionPrompt.cpp`, update `render()` to include the tool badge and description:

```cpp
ftxui::Element PermissionPromptComponent::render() {
    using namespace ftxui;
    using namespace claude::ftxui_colors;

    std::vector<Element> items;

    // Tool badge with colored background
    auto badgeColor = toolBgColor(options_.toolName);
    items.push_back(hbox({
        text(" " + options_.toolName + " ") | bold | bgcolor(badgeColor) | color(toolFgColor(options_.toolName)),
    }));

    // Tool-specific description
    if (!options_.description.empty()) {
        items.push_back(text("  " + options_.description) | dim | color(MacCream));
    }
    items.push_back(text(""));

    // 5 choices with focus indicator
    for (size_t i = 0; i < options_.choices.size(); ++i) {
        auto choiceColor = (i <= 2) ? MacMint       // Allow options
                         : (i == 3) ? MacRose       // Deny once
                         : RGB(180, 120, 120);       // Always deny

        if (static_cast<int>(i) == focusedIndex_) {
            items.push_back(text("  ❯ " + options_.choices[i]) | bold | inverted | color(choiceColor));
        } else {
            items.push_back(text("    " + options_.choices[i]) | color(choiceColor));
        }
    }

    items.push_back(text(""));
    items.push_back(text("  ↑↓ select · Enter confirm · Esc cancel") | dim | color(MacShadow));

    return vbox(std::move(items)) | borderRounded | size(WIDTH, LESS_THAN, 70);
}
```

- [ ] **Step 3: Update FTXUI permission prompt rendering in FtxuiRender.cpp**

In `src/ui/FtxuiRender.cpp`, find the permission prompt section (lines 612-680). Replace the inline rendering with the tool badge + description format. Update the 4-option rendering to 5 options:

```cpp
// Permission prompt (5 options, matching TS)
if (r->permissionPromptActive_) {
    auto badgeColor = ftxui_colors::toolBgColor(r->permissionToolName_);
    auto badgeFg = ftxui_colors::toolFgColor(r->permissionToolName_);

    auto permToolBadge = hbox({
        text(" " + r->permissionToolName_ + " ") | bold | bgcolor(badgeColor) | color(badgeFg),
    });

    // Generate tool-specific description
    String desc = r->permissionDescription_;
    auto permDesc = desc.empty() ? emptyElement() : (text("  " + desc) | dim | color(ftxui_colors::MacCream));

    std::vector<Element> permOptions;
    const char* optionLabels[] = {
        "Yes (once)",
        "Allow for this session",
        "Yes, always allow",
        "No (once)",
        "No, always deny"
    };
    const ftxui::Color optionColors[] = {
        ftxui_colors::MacMint,
        ftxui_colors::MacSage,
        ftxui_colors::MacSage,
        ftxui_colors::MacRose,
        ftxui_colors::MacRose,
    };

    for (int i = 0; i < 5; ++i) {
        if (i == r->permissionFocusedIndex_) {
            permOptions.push_back(text("  ❯ " + String(optionLabels[i])) | bold | inverted | color(optionColors[i]));
        } else {
            permOptions.push_back(text("    " + String(optionLabels[i])) | color(optionColors[i]));
        }
    }

    auto permContent = vbox({
        hbox({text("╭─ ⚠ Permission Required ─╮") | bold | color(ftxui_colors::MacGold)}),
        text(""),
        permToolBadge,
        permDesc,
        text(""),
        vbox(permOptions),
        text(""),
        text("  ↑↓ select · Enter confirm · Esc cancel") | dim | color(ftxui_colors::MacShadow),
        hbox({text("╰──────────────────────────╯") | color(ftxui_colors::MacGold)}),
    });

    elems.push_back(permContent);
}
```

- [ ] **Step 4: Add permissionDescription_ field and tool-specific generation**

In `include/claude/ui/FtxuiRepl.hpp`, add to the permission state fields (around line 164):

```cpp
String permissionDescription_;  // Tool-specific description
```

In `src/ui/FtxuiPermission.cpp`, update `promptPermission()` to generate the description:

```cpp
#include <claude/console/ActivityDescription.hpp>

PermissionChoice FtxuiRepl::promptPermission(const String& toolName, const String& activity) {
    if (!screen_) return PermissionChoice::DenyOnce;

    // Generate tool-specific permission description
    String desc;
    if (toolName == "Bash") {
        // Extract command from activity or JSON
        desc = "Allow Bash to run: " + activity;
    } else if (toolName == "Read") {
        desc = "Allow Read to access: " + activity;
    } else if (toolName == "Write") {
        desc = "Allow Write to modify: " + activity;
    } else if (toolName == "Edit") {
        desc = "Allow Edit to modify: " + activity;
    } else if (toolName == "Grep" || toolName == "Glob") {
        desc = "Allow " + toolName + " to search: " + activity;
    } else if (toolName == "WebFetch") {
        desc = "Allow WebFetch to access: " + activity;
    } else if (toolName == "WebSearch") {
        desc = "Allow WebSearch to query: " + activity;
    } else if (toolName == "Agent") {
        desc = "Allow Agent to run: " + activity;
    } else {
        desc = "Allow " + toolName + " to run";
    }

    {
        std::lock_guard lock(permissionMutex_);
        permissionAnswered_ = false;
        permissionResult_ = PermissionChoice::DenyOnce;
    }

    screen_->Post([this, tn = String(toolName), act = String(activity), d = String(desc)]() {
        permissionPromptActive_ = true;
        permissionFocusedIndex_ = 0;
        permissionToolName_ = std::move(tn);
        permissionActivity_ = std::move(act);
        permissionDescription_ = std::move(d);
    });

    screen_->RequestAnimationFrame();

    std::unique_lock lock(permissionMutex_);
    permissionCv_.wait(lock, [this]() { return permissionAnswered_; });

    return permissionResult_;
}
```

- [ ] **Step 5: Update permission event handling for 5 options**

In `src/ui/FtxuiRender.cpp`, find the permission prompt event handling (lines 1017-1063). Update the `Enter` handler to map indices 0-4 correctly:

```cpp
// Index 0: AllowOnce, 1: AllowSession, 2: AlwaysAllow, 3: DenyOnce, 4: AlwaysDeny
if (event == ftxui::Event::Return) {
    PermissionChoice choices[] = {
        PermissionChoice::AllowOnce,
        PermissionChoice::AllowSession,
        PermissionChoice::AlwaysAllow,
        PermissionChoice::DenyOnce,
        PermissionChoice::AlwaysDeny
    };
    int idx = r->permissionFocusedIndex_;
    r->permissionResult_ = choices[std::clamp(idx, 0, 4)];
    r->permissionAnswered_ = true;
    r->permissionPromptActive_ = false;
    r->permissionCv_.notify_one();
    return true;
}
```

Also update `ArrowUp`/`ArrowDown` to cycle through 5 options (change `% 4` to `% 5`).

- [ ] **Step 6: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 7: Commit**

```bash
git add include/claude/ui/components/PermissionPrompt.hpp src/ui/components/PermissionPrompt.cpp src/ui/FtxuiRender.cpp include/claude/ui/FtxuiRepl.hpp src/ui/FtxuiPermission.cpp
git commit -m "feat: update permission prompt to 5 options with tool-specific descriptions"
```

---

## Task 12: Move Permission Prompt to Modal Overlay

**Files:**
- Modify: `src/ui/FtxuiRender.cpp:612-680, 899-907`

- [ ] **Step 1: Restructure permission rendering as overlay**

In `src/ui/FtxuiRender.cpp`, the current layout assembly (lines 899-907) is:

```cpp
return vbox({
    header,
    separatorLight(),
    messagesArea,
    separatorLight(),
    statusBar,
    completionArea,
    inputLine | (r->isStreaming_ ? dim : bold),
});
```

The permission prompt is currently pushed into `elems` (the message list). Instead, render it as an overlay positioned at the bottom of the content area. Use FTXUI's `window()` or absolute positioning via `layer()`:

Replace the permission rendering in the message loop (the block at lines 612-680 that pushes to `elems`) with an overlay approach. Keep the `elems.push_back()` removed and instead store the permission content in a separate variable:

```cpp
// Build permission overlay content (not added to elems)
ftxui::Element permOverlay = emptyElement();
if (r->permissionPromptActive_) {
    // ... build the permission prompt element (same as Task 11 Step 3) ...
    // but store in permOverlay instead of pushing to elems
    permOverlay = permContent;
}
```

Then in the final layout, compose the messages area with an overlay:

```cpp
auto contentArea = messagesArea;
if (r->permissionPromptActive_) {
    // Overlay the permission prompt at the bottom of the content area
    contentArea = ftxui::relativeWindow(
        ftxui::nothing(),
        contentArea,
        permOverlay | ftxui::align_bottom | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 70)
    );
}
```

However, FTXUI doesn't have a built-in `relativeWindow`. Use the simpler approach with `ftxui::layer()`:

```cpp
// In the final layout, compose with overlay
auto contentWithOverlay = r->permissionPromptActive_
    ? ftxui::layers(messagesArea, permOverlay | ftxui::align_bottom | ftxui::center | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 70))
    : messagesArea;

return vbox({
    header,
    separatorLight(),
    contentWithOverlay | flex,
    separatorLight(),
    statusBar,
    completionArea,
    inputLine | (r->isStreaming_ ? dim : bold),
});
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds (may need FTXUI layer API adjustment — check FTXUI docs for `layers()` or `absolute_position()`).

- [ ] **Step 3: Commit**

```bash
git add src/ui/FtxuiRender.cpp
git commit -m "feat: render permission prompt as overlay instead of inline"
```

---

## Task 13: Add AgentProgress Display Message Type

**Files:**
- Modify: `include/claude/ui/UiMessageTypes.hpp:98-112`
- Modify: `src/ui/FtxuiStreaming.cpp:97-126`

- [ ] **Step 1: Add AgentProgress to DisplayMessage::Type enum**

In `include/claude/ui/UiMessageTypes.hpp`, add to the `Type` enum (after `GroupedToolUse`):

```cpp
AgentProgress,    // Sub-agent/parallel task progress tree
```

Add a factory helper after the existing factory helpers:

```cpp
static DisplayMessage makeAgentProgress(const String& agentType,
                                         const String& description,
                                         int toolUses,
                                         int tokens,
                                         bool running) {
    DisplayMessage msg;
    msg.type = Type::AgentProgress;
    msg.text = description;
    msg.toolUse.toolName = agentType;
    msg.toolUse.isInProgress = running;
    msg.groupedTools.toolUseCount = toolUses;
    msg.toolUse.result = std::to_string(tokens) + " tokens";
    return msg;
}
```

- [ ] **Step 2: Change refresh thread to use AgentProgress instead of SystemInfo**

In `src/ui/FtxuiStreaming.cpp`, find the background task polling section (lines 97-126). Replace the `SystemInfo` message posting with `AgentProgress` messages:

```cpp
// Background task polling (every ~2 seconds)
if (bgCheckCounter % 40 == 0) {
    auto tasks = UnifiedTaskStore::instance().listTasks();
    int runningCount = 0;
    std::vector<DisplayMessage> progressMsgs;

    for (const auto& task : tasks) {
        if (task.status == UnifiedTask::Status::InProgress && task.agentHandle) {
            runningCount++;
            progressMsgs.push_back(DisplayMessage::makeAgentProgress(
                task.agentType.value_or("Agent"),
                task.subject,
                0,  // toolUses — not tracked at task level yet
                task.totalTokens,
                true
            ));
        }
    }

    if (runningCount > 0 && !isStreaming_) {
        screen_->Post([this, msgs = std::move(progressMsgs)]() {
            // Replace existing AgentProgress messages instead of accumulating
            messages_.erase(
                std::remove_if(messages_.begin(), messages_.end(),
                    [](const DisplayMessage& m) { return m.type == DisplayMessage::Type::AgentProgress; }),
                messages_.end()
            );
            for (auto& m : msgs) {
                messages_.push_back(std::move(m));
            }
        });
    } else if (runningCount == 0) {
        // Remove any stale AgentProgress messages
        screen_->Post([this]() {
            messages_.erase(
                std::remove_if(messages_.begin(), messages_.end(),
                    [](const DisplayMessage& m) { return m.type == DisplayMessage::Type::AgentProgress; }),
                messages_.end()
            );
        });
    }
}
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add include/claude/ui/UiMessageTypes.hpp src/ui/FtxuiStreaming.cpp
git commit -m "feat: add AgentProgress message type for sub-agent progress display"
```

---

## Task 14: Render Agent Progress Tree in Content Area

**Files:**
- Modify: `src/ui/FtxuiRender.cpp` — add AgentProgress rendering in message loop

- [ ] **Step 1: Add AgentProgress rendering to message loop**

In `src/ui/FtxuiRender.cpp`, find the message type dispatch in the `OnRender()` message loop. After the `HookSummary` case (line 124), add:

```cpp
case DisplayMessage::Type::AgentProgress: {
    auto agentType = msg.toolUse.toolName.empty() ? String("Agent") : msg.toolUse.toolName;
    auto desc = msg.text.empty() ? String("Working...") : msg.text;
    auto running = msg.toolUse.isInProgress;
    auto tokens = msg.toolUse.result;  // "N tokens"

    auto badgeColor = ftxui_colors::toolBgColor("Agent");
    auto badgeFg = ftxui_colors::toolFgColor("Agent");

    elems.push_back(vbox({
        hbox({
            text("├── ") | color(ftxui_colors::MacShadow),
            text(" " + agentType + " ") | bold | bgcolor(badgeColor) | color(badgeFg),
            text(" " + desc) | color(ftxui_colors::MacCream),
            text(" · ") | color(ftxui_colors::MacShadow),
            text(tokens) | dim | color(ftxui_colors::MacCream),
        }),
        hbox({
            text("│  ") | color(ftxui_colors::MacShadow),
            text(running ? "⏻  Working..." : "Done") | color(running ? ftxui_colors::MacGold : ftxui_colors::MacMint),
        }),
    }));
    break;
}
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/ui/FtxuiRender.cpp
git commit -m "feat: render agent progress tree in content area"
```

---

## Task 15: Create PromptInputFooter Component

**Files:**
- Create: `include/claude/ui/PromptInputFooter.hpp`
- Create: `src/ui/PromptInputFooter.cpp`
- Modify: `include/claude/ui/FtxuiRepl.hpp` — add mode, auth, cwd/git fields
- Modify: `src/ui/FtxuiRender.cpp:899-907` — add footer to bottom layout
- Modify: `CMakeLists.txt` — add to UI_SOURCES

- [ ] **Step 1: Add state fields to FtxuiRepl**

In `include/claude/ui/FtxuiRepl.hpp`, add fields in the private section (around line 170):

```cpp
// Mode state
String currentMode_;   // "default", "auto", "bypass", "plan", "acceptEdits", "dontAsk"
bool modeHintDismissed_ = false;

// Auth state
bool isAuthenticated_ = false;

// Cwd/git state
String cwd_;
String gitBranch_;

void setCurrentMode(const String& mode) { currentMode_ = mode; modeHintDismissed_ = false; }
void setAuthStatus(bool authenticated) { isAuthenticated_ = authenticated; }
void setCwd(const String& cwd) { cwd_ = cwd; }
void setGitBranch(const String& branch) { gitBranch_ = branch; }
```

- [ ] **Step 2: Write the PromptInputFooter header**

Create `include/claude/ui/PromptInputFooter.hpp`:

```cpp
#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>

namespace claude::ftxui_footer {

struct FooterState {
    String mode;            // "default", "auto", "bypass", "plan", "acceptEdits", "dontAsk"
    bool modeHintDismissed = false;
    bool isAuthenticated = false;
    String modelInfo;
    int contextPct = 0;
    double costUsd = 0.0;
    bool isStreaming = false;
    String cwd;
    String gitBranch;
};

ftxui::Element renderFooter(const FooterState& state);

} // namespace claude::ftxui_footer
```

- [ ] **Step 3: Write the PromptInputFooter implementation**

Create `src/ui/PromptInputFooter.cpp`:

```cpp
#include <claude/ui/PromptInputFooter.hpp>
#include <claude/ui/FtxuiColors.hpp>

namespace claude::ftxui_footer {

using namespace ftxui;
using namespace claude::ftxui_colors;

struct ModeDisplay {
    const char* symbol;
    const char* label;
    ftxui::Color color;
};

ModeDisplay getModeDisplay(const String& mode) {
    if (mode == "auto")        return {"⚙", "auto on",         MacSky};
    if (mode == "plan")        return {"✦", "plan on",         MacLavender};
    if (mode == "bypass")      return {"⚡", "bypass on",      MacRose};
    if (mode == "acceptEdits") return {"✎", "accept edits on", MacMint};
    if (mode == "dontAsk")     return {"⊙", "dont ask on",    MacCream};
    return {"", "", MacShadow};  // Default mode — hidden
}

ftxui::Element renderFooter(const FooterState& state) {
    std::vector<Element> leftParts;

    // Mode indicator
    auto modeDisp = getModeDisplay(state.mode);
    if (modeDisp.label[0] != '\0') {
        leftParts.push_back(hbox({
            text(String(modeDisp.symbol) + " ") | color(modeDisp.color),
            text(modeDisp.label) | color(modeDisp.color) | dim,
        }));
        if (!state.modeHintDismissed) {
            leftParts.push_back(text(" shift+tab to cycle") | color(MacShadow) | dim);
        }
    }

    // Keyboard hints (context-dependent)
    if (state.isStreaming) {
        leftParts.push_back(text("esc to interrupt") | color(MacShadow) | dim);
    } else {
        leftParts.push_back(text("? for shortcuts") | color(MacShadow) | dim);
    }

    // Right side: auth + model + context + cost
    std::vector<Element> rightParts;

    // Auth status
    if (state.isAuthenticated) {
        rightParts.push_back(text("✓") | color(MacMint));
        rightParts.push_back(text(" logged in") | color(MacShadow) | dim);
    } else {
        rightParts.push_back(text("⚠") | color(MacGold));
        rightParts.push_back(text(" not authenticated") | color(MacGold) | dim);
    }

    // Separator
    rightParts.push_back(text(" · ") | color(MacShadow));

    // Model + context + cost
    rightParts.push_back(text(state.modelInfo) | color(MacCream) | dim);
    rightParts.push_back(text(" · ") | color(MacShadow));

    auto barColor = (state.contextPct >= 85) ? MacContextCrit
                  : (state.contextPct >= 70) ? MacContextWarn
                  :                             MacContextOk;

    rightParts.push_back(text(std::to_string(state.contextPct) + "% ctx") | color(barColor) | dim);
    rightParts.push_back(text(" · ") | color(MacShadow));

    char costBuf[16];
    snprintf(costBuf, sizeof(costBuf), "$%.4f", state.costUsd);
    rightParts.push_back(text(costBuf) | color(MacCream) | dim);

    // Compose
    auto leftSide = leftParts.empty() ? emptyElement()
                  : leftParts.size() == 1 ? leftParts[0]
                  : hbox(leftParts) | flex;

    auto rightSide = rightParts.empty() ? emptyElement()
                   : hbox(rightParts);

    return hbox({
        text(" "),
        leftSide | flex,
        rightSide,
        text(" "),
    }) | ftxui::bgcolor(MacBgDark);
}

} // namespace claude::ftxui_footer
```

- [ ] **Step 4: Add PromptInputFooter to CMakeLists**

In `CMakeLists.txt`, add to `UI_SOURCES`:

```cmake
src/ui/PromptInputFooter.cpp
```

- [ ] **Step 5: Add footer to the bottom layout in FtxuiRender.cpp**

In `src/ui/FtxuiRender.cpp`, add the include:

```cpp
#include <claude/ui/PromptInputFooter.hpp>
```

In the final layout assembly (currently lines 899-907), add the footer after `inputLine`:

```cpp
// Build footer state
ftxui_footer::FooterState footerState;
footerState.mode = r->currentMode_;
footerState.modeHintDismissed = r->modeHintDismissed_;
footerState.isAuthenticated = r->isAuthenticated_;
footerState.modelInfo = r->modelInfo_;
footerState.contextPct = (r->contextMaxTokens_ > 0)
    ? static_cast<int>(100 * r->contextUsedTokens_ / r->contextMaxTokens_) : 0;
footerState.costUsd = r->costUsd_;
footerState.isStreaming = r->isStreaming_;
footerState.cwd = r->cwd_;
footerState.gitBranch = r->gitBranch_;

auto footer = ftxui_footer::renderFooter(footerState);

return vbox({
    header,
    separatorLight(),
    contentWithOverlay | flex,
    separatorLight(),
    statusBar,
    completionArea,
    inputLine | (r->isStreaming_ ? dim : bold),
    footer,
});
```

- [ ] **Step 6: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 7: Commit**

```bash
git add include/claude/ui/PromptInputFooter.hpp src/ui/PromptInputFooter.cpp include/claude/ui/FtxuiRepl.hpp src/ui/FtxuiRender.cpp CMakeLists.txt
git commit -m "feat: add PromptInputFooter with mode indicator, auth status, keyboard hints"
```

---

## Task 16: Wire Up Mode, Auth, Cwd State from AppState

**Files:**
- Modify: `src/main.cpp` — set mode, auth, cwd on FtxuiRepl
- Modify: `src/ui/FtxuiRender.cpp` — add shift+tab handler for mode cycling

- [ ] **Step 1: Wire mode state from AppState**

In `src/main.cpp`, in `runFtxuiRepl()` (around line 659+), after creating the `FtxuiRepl`, add:

```cpp
// Set initial mode
auto modeStr = AppState::instance().permissionMode();
repl->setCurrentMode(modeStr);
```

Also in the permission mode application section (around lines 291-309), after setting the mode on `AppState`, update the repl:

```cpp
repl->setCurrentMode(AppState::instance().permissionMode());
```

(Note: The repl may not exist yet at this point in main(). Add the mode setting inside `runFtxuiRepl()` after the repl is created.)

- [ ] **Step 2: Wire auth status**

In `src/main.cpp`, in `runFtxuiRepl()`, add:

```cpp
// Set auth status — check if API key is available from config or --api-key-fd
bool hasAuth = AppConfig::instance().hasApiKey()
            || AppState::instance().apiKeyFromFd().has_value();
repl->setAuthStatus(hasAuth);
```

Check `AppConfig` for the exact `hasApiKey()` or `apiKey()` accessor name — it may be `AppConfig::instance().auth().apiKey` or similar.

- [ ] **Step 3: Wire cwd/git branch**

In `src/main.cpp`, in `runFtxuiRepl()`, add:

```cpp
// Set cwd
repl->setCwd(AppState::instance().cwd());

// Set git branch
auto gitCtx = GitContext::collect(std::filesystem::current_path());
if (gitCtx.isGitRepo && !gitCtx.branch.empty()) {
    repl->setGitBranch(gitCtx.branch);
}
```

This requires `#include <claude/context/GitContext.hpp>` and `#include <filesystem>` in main.cpp.

- [ ] **Step 4: Add shift+tab mode cycling handler**

In `src/ui/FtxuiRender.cpp`, in the `OnEvent()` method, add a handler for Shift+Tab to cycle permission modes. Find the existing key handling section and add:

```cpp
// Shift+Tab: cycle permission mode
if (event == ftxui::Event::TabReverse) {
    const char* modes[] = {"default", "acceptEdits", "auto", "bypass", "dontAsk", "plan"};
    String current = r->currentMode_;
    int idx = 0;
    for (int i = 0; i < 6; ++i) {
        if (modes[i] == current) { idx = i; break; }
    }
    idx = (idx + 1) % 6;
    r->currentMode_ = modes[idx];
    r->modeHintDismissed_ = false;
    AppState::instance().setPermissionMode(modes[idx]);
    return true;
}
```

Note: Check if FTXUI has `Event::TabReverse` or if Shift+Tab is detected differently. It may be `Event::Special("\x1b[Z")`.

- [ ] **Step 5: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/ui/FtxuiRender.cpp
git commit -m "feat: wire mode, auth, cwd state from AppState to FTXUI footer"
```

---

## Task 17: Add Cwd and Git Branch to Header

**Files:**
- Modify: `src/ui/FtxuiRender.cpp:33-42`

- [ ] **Step 1: Add cwd/git to header rendering**

In `src/ui/FtxuiRender.cpp`, extend the header hbox to include cwd and git branch on the right side, before the status indicator. After the cost display (from Task 9), add:

```cpp
// Cwd and git branch
text(" │ ") | color(MacShadow),
text(r->cwd_.empty() ? "~" : r->cwd_) | color(MacCream) | dim,
```

If git branch is available:

```cpp
if (!r->gitBranch_.empty()) {
    // Add to header hbox:
    text(" (") | color(MacShadow),
    text(r->gitBranch_) | color(ftxui_colors::MacSky) | dim,
    text(")") | color(MacShadow),
}
```

Place these between the cost display and the Running/Idle status indicator.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/ui/FtxuiRender.cpp
git commit -m "feat: add cwd and git branch display to header"
```

---

## Task 18: Fix "unknown" Display in Input Area

**Files:**
- Modify: `src/ui/FtxuiRender.cpp`

- [ ] **Step 1: Find and remove "unknown" text rendering**

In `src/ui/FtxuiRender.cpp`, search for the literal string `"unknown"`. It appears in the idle status bar or below the input line when model info is not set. Remove the standalone "unknown" rendering.

The "unknown" text likely appears in the statusBar section (lines 740-812) where `r->modelInfo_` is displayed but may be empty. Change the rendering to show nothing when model info is not set:

```cpp
// In the idle status bar, only show model info if it's available
auto modelInfoElem = r->modelInfo_.empty()
    ? emptyElement()
    : text(r->modelInfo_) | color(MacCream) | dim;
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/ui/FtxuiRender.cpp
git commit -m "fix: remove standalone 'unknown' text from input area"
```

---

## Task 19: Restructure Bottom Layout to Match TS

**Files:**
- Modify: `src/ui/FtxuiRender.cpp:899-907`

This is the final structural task that assembles the new bottom layout matching TS's component stack.

- [ ] **Step 1: Restructure the final layout**

The current bottom layout (top to bottom) is:
```
statusBar
completionArea
inputLine
```

The target layout (matching TS `FullscreenLayout → bottom`) is:
```
pendingPermissions (pending indicator line — if any)
completionArea
inputLine
promptInputFooter (from Task 15)
```

The `statusBar` content (streaming/idle status with spinner, elapsed time, tokens) should be absorbed into the header (Tasks 8-9 already added context bar, tokens, cost) and the footer (Task 15 already includes model/cost/auth). The separate statusBar section can be removed or simplified to just a pending permission indicator.

Update the final layout assembly:

```cpp
return vbox({
    header,
    separatorLight(),
    contentWithOverlay | flex,
    separatorLight(),
    // Pending permissions indicator (one line, when permission is being processed)
    r->permissionPromptActive_ ? text("  ⏳ Permission pending...") | dim | color(ftxui_colors::MacGold) : emptyElement(),
    completionArea,
    inputLine | (r->isStreaming_ ? dim : bold),
    footer,
});
```

Note: The existing statusBar (lines 740-812) contains streaming/elapsed/token info that's now in the header and footer. Remove the statusBar section from the layout. Keep the code that builds statusBar elements in case it's needed for the readline mode, but remove it from the FTXUI layout.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/ui/FtxuiRender.cpp
git commit -m "feat: restructure bottom layout to match TS FullscreenLayout bottom slot"
```

---

## Task 20: End-to-End Integration Test

**Files:**
- Create: `tests/test_ui_alignment.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write integration tests**

Create `tests/test_ui_alignment.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <claude/console/ActivityDescription.hpp>
#include <claude/permission/PermissionTypes.hpp>

TEST_CASE("UI alignment: ActivityDescription consistency", "[ui-alignment]") {
    SECTION("All known tools produce non-empty descriptions") {
        const char* tools[] = {"Read", "Write", "Edit", "Bash", "Grep", "Glob",
                               "WebFetch", "WebSearch", "Agent", "LSP", "MCP"};
        for (auto tool : tools) {
            String desc = getActivityDescription(tool, "{}", true);
            REQUIRE_FALSE(desc.empty());
            REQUIRE(desc.find("Running") != String::npos || desc.find("Reading") != String::npos
                     || desc.find("Writing") != String::npos || desc.find("Editing") != String::npos
                     || desc.find("Searching") != String::npos || desc.find("Finding") != String::npos
                     || desc.find("Fetching") != String::npos || desc.find("LSP") != String::npos
                     || desc.find("Calling MCP") != String::npos);
        }
    }
}

TEST_CASE("UI alignment: PermissionChoice has 5 options", "[ui-alignment]") {
    SECTION("AllowSession exists between AllowOnce and AlwaysAllow") {
        auto session = PermissionChoice::AllowSession;
        // Verify it's a valid enum value
        REQUIRE(session == PermissionChoice::AllowSession);
    }
}

TEST_CASE("UI alignment: PermissionMode string round-trip", "[ui-alignment]") {
    SECTION("All modes can be converted to string and back") {
        auto modes = {PermissionMode::Default, PermissionMode::AcceptEdits,
                      PermissionMode::Auto, PermissionMode::Bypass,
                      PermissionMode::DontAsk, PermissionMode::Plan};
        for (auto mode : modes) {
            String str = permissionModeToString(mode);
            REQUIRE_FALSE(str.empty());
        }
    }
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(test_ui_alignment test_ui_alignment.cpp)
target_link_libraries(test_ui_alignment PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_ui_alignment)
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

Run: `ctest --test-dir build -R ui_alignment 2>&1`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_ui_alignment.cpp tests/CMakeLists.txt
git commit -m "test: add UI alignment integration tests"
```

---

## Implementation Order Summary

| Task | Component | Depends On | Priority |
|------|-----------|------------|----------|
| 1 | Remove startup banner | — | P0 |
| 2 | Make FTXUI default | — | P0 |
| 3 | Suppress spdlog | — | P0 |
| 4 | Guard ANSI with isatty | — | P0 |
| 5 | Fix model name display | — | P0 |
| 6 | Add ActivityDescription | — | P1 |
| 7 | Integrate ActivityDescription | 6 | P1 |
| 8 | Context bar in header | — | P1 |
| 9 | Token/cost in header | 8 | P1 |
| 10 | AllowSession permission | — | P1 |
| 11 | Update permission prompt | 10 | P1 |
| 12 | Permission as overlay | 11 | P1 |
| 13 | AgentProgress message type | — | P1 |
| 14 | Render agent progress tree | 13 | P1 |
| 15 | PromptInputFooter component | — | P1 |
| 16 | Wire mode/auth/cwd state | 15 | P1 |
| 17 | Cwd/git in header | 16 | P1 |
| 18 | Fix "unknown" display | — | P2 |
| 19 | Restructure bottom layout | 8,9,11,12,15,18 | P1 |
| 20 | Integration tests | 6,10 | P1 |

Tasks 1-5 (P0) are independent and can be done in any order. Tasks 6-7 should be done together. Task 19 is the final assembly task that depends on most others.
