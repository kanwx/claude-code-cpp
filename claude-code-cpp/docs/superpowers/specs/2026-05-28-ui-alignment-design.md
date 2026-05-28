# UI Alignment Design Spec

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Align claude-code-cpp's terminal UI with the official Claude Code TS implementation — three-region fullscreen TUI layout with correct bottom area, header, and infrastructure.

**Architecture:** Make FTXUI the default UI mode. Restructure the FTXUI bottom slot to match TS's `FullscreenLayout → bottom` component stack. Add missing UI elements (mode indicator, permission options, parallel task progress, auth status). Remove startup banner. Fix infrastructure issues (spdlog, isatty guards, model name display).

**Tech Stack:** FTXUI (C++ terminal UI framework), existing macaron color palette, existing FtxuiRepl class, existing AgentLoop callback system.

---

## Part 1: Infrastructure Fixes (P0)

These are prerequisite fixes that affect all UI modes.

### 1.1 Remove startup banner

**Problem:** Every startup shows 10+ lines of ASCII art logo + welcome text. Official Claude Code shows only the prompt.

**Change:** Delete `BannerPrinter::print()` and `BannerPrinter::printWelcome()` bodies (leave as empty functions for ABI compat). In FTXUI mode, the welcome screen box in FtxuiRender.cpp (lines 682-696) should be replaced with an empty content area — show nothing until the first message arrives. In readline mode, `main.cpp` should not call `BannerPrinter::printWelcome()`.

**Files:**
- `src/console/BannerPrinter.cpp` — empty the print/printWelcome bodies
- `src/ui/FtxuiRender.cpp` — remove welcome screen box, show empty area
- `src/main.cpp` — remove BannerPrinter calls

### 1.2 Make FTXUI the default mode

**Problem:** `useFtxui_` defaults to `false`. Users must pass `--ftxui` to get the proper TUI. Official Claude Code always uses fullscreen TUI.

**Change:** When `HAS_FTXUI` is compiled in, set `useFtxui_ = true` by default. Add a `--no-ftxui` flag to force readline mode. When stdout is not a TTY (`!isatty(STDOUT_FILENO)`), automatically fall back to readline mode regardless of the flag.

**Files:**
- `src/main.cpp` — change `bool useFtxui_ = false` to `bool useFtxui_ = true` inside `#ifdef HAS_FTXUI`, add isatty() guard and `--no-ftxui` flag

### 1.3 Suppress spdlog output in terminal

**Problem:** Info-level log messages like `[info] Session saved to...` and `[info] FTXUI: Building component...` leak into the visible terminal output.

**Change:** Set spdlog default level to `err` at program start (before any components initialize). Add a `--verbose` flag that restores `info` level. Log file output remains at `debug` level for diagnostics — write to `~/.claude/logs/claude-cli.log`.

**Files:**
- `src/main.cpp` — add `spdlog::set_level(spdlog::level::err)` early in main(), add log file sink
- Remove or guard all `spdlog::info()` calls that write to stderr during normal operation

### 1.4 Guard ANSI escape sequences with isatty()

**Problem:** Mouse disable sequences (`[?1000l`), cursor restore (`[?25h`), and status line cursor commands (`[s`/`[u`) appear as literal text when output is piped or terminal doesn't support them.

**Change:** In `restoreTerminal()` and `StatusLine`, only write escape sequences when `isatty(STDOUT_FILENO)` is true. In non-TTY mode, skip all ANSI output entirely.

**Files:**
- `src/bootstrap/SignalHandler.cpp` — guard `restoreTerminal()` with isatty check
- `src/console/StatusLine.cpp` — guard all output with isatty check

### 1.5 Fix model name display

**Problem:** Header and status bar show raw model ID like "glm-5" instead of a human-readable display name. Status bar shows "unknown".

**Change:** When `ModelStrings` is set in AppState, use `displayName` field for display. When not set, use the full model ID (e.g., "claude-sonnet-4-20250514"). In FtxuiRepl, `setModelInfo()` should accept the display name. The header should show the short display name (e.g., "Sonnet 4"), the status bar the full name.

**Files:**
- `src/ui/FtxuiRender.cpp` — use display name in header
- `src/main.cpp` — pass display name to `setModelInfo()`
- `src/console/StatusLine.cpp` — use display name

---

## Part 2: Header Bar Enhancement (P1)

### 2.1 Add context usage progress bar

**Problem:** Header only shows model name text. Official shows a visual progress bar for context window usage.

**Change:** Add a visual progress bar to the header between model name and status indicator. The bar uses block characters: `█` for used, `░` for remaining. Color shifts: green (<70%), yellow (70-85%), red (>=85%). Show percentage label after the bar.

Header format: `╭─ Claude Code │ sonnet-4 │ ████████░░ 42% ctx │ ○ Idle ─╮`

**Files:**
- `src/ui/FtxuiRender.cpp` — add progress bar Element to header layout
- `src/ui/FtxuiColors.hpp` — add semantic colors for context bar thresholds (green/yellow/red)

### 2.2 Add token count and cost to header

**Problem:** Cost and token counts only appear in the idle status bar. Official shows them in the header.

**Change:** Add token counts (`3.2K in/1.1K out`) and cost (`$0.0048`) to the header bar, after the context bar.

**Files:**
- `src/ui/FtxuiRender.cpp` — extend header with token/cost Elements
- `include/claude/ui/FtxuiRepl.hpp` — add token count accessor methods

---

## Part 3: Bottom Area Alignment (P1)

This is the most critical change — restructuring the bottom slot to match TS's `FullscreenLayout → bottom` component stack.

### 3.1 Restructure bottom layout

**Current bottom layout (top to bottom):**
```
statusBar
completionArea
inputLine
```

**Target bottom layout (matching TS):**
```
permissionOverlay (when active — modal over content area, not inline)
pendingPermissions (worker pending indicator — one line)
completionArea
inputLine
promptInputFooter
```

The `promptInputFooter` is a new component with this layout:
```
[ModeIndicator] [BackgroundTaskStatus] [KeyboardHints]    [ModelInfo · Context% · Cost]
```

**Files:**
- `src/ui/FtxuiRender.cpp` — restructure MainComponent bottom layout
- Create: `include/claude/ui/PromptInputFooter.hpp` — new component class
- Create: `src/ui/PromptInputFooter.cpp` — implementation

### 3.2 Add ModeIndicator

**Problem:** No visual indicator for the current permission mode (plan, auto, bypass, default). Official shows `⚙ auto on`, `✦ plan on`, `⚡ bypass on`.

**Change:** Add a mode indicator to `PromptInputFooter`. Display format matches TS:

| Mode | Symbol | Color | Display |
|------|--------|-------|---------|
| Default | (none) | — | (hidden) |
| AcceptEdits | `✎` | MacMint | `✎ accept edits on` |
| Auto | `⚙` | MacSky | `⚙ auto on` |
| Bypass | `⚡` | MacRose | `⚡ bypass on` |
| DontAsk | `⊙` | MacCream | `⊙ dont ask on` |
| Plan | `✦` | MacLavender | `✦ plan on` |

Append `shift+tab to cycle` hint in dim text when mode is active and user hasn't dismissed.

Source for mode: `AppState::instance().permissionMode()` → `PermissionMode` enum.

**Files:**
- `include/claude/ui/FtxuiRepl.hpp` — add mode state tracking
- `src/ui/FtxuiRender.cpp` — render mode indicator in footer

### 3.3 Add "Allow for this session" permission option

**Problem:** Current permission prompt has 4 options. Official has "Allow for this session" as the second option (between "Yes (once)" and "Yes, always allow").

**Change:** Add `PermissionChoice::AllowSession` to the `PermissionChoice` enum. The permission prompt should show 5 options:
1. Yes (once)
2. Allow for this session
3. Yes, always allow
4. No (once)
5. No, always deny

"Allow for this session" creates a session-scoped allow rule (stored in `PermissionSettings` under `Session` source) that persists for the current session but is not saved to disk.

**Files:**
- `include/claude/permission/PermissionTypes.hpp` — add `AllowSession` to `PermissionChoice` enum
- `src/ui/FtxuiRender.cpp` — add 5th option to permission prompt rendering
- `src/permission/PermissionSettings.cpp` — handle AllowSession rule creation
- `src/permission/RuleEngine.cpp` — evaluate session-scoped rules

### 3.4 Move permission prompt to modal overlay

**Problem:** Permission prompt is rendered inline in the message stream. Official renders it as a modal overlay over the content area, with the user able to scroll up for context.

**Change:** When `permissionPromptActive_` is true, render the permission prompt as an overlay positioned at the bottom of the content area (matching TS `overlay` prop). Use `ftxui::Component` with absolute positioning. The existing `ModalOverlayComponent` in `components/ModalOverlay.cpp` can be used or adapted.

Format the permission prompt to show:
- Tool badge (per-tool colored background + name)
- Tool-specific description (matching TS `BashPermissionRequest`, `FileEditPermissionRequest`, etc.):
  - Bash: `Allow Bash to run: <command>`
  - Read: `Allow Read to access: <file_path>`
  - Write: `Allow Write to modify: <file_path>`
  - Edit: `Allow Edit to modify: <file_path>` + old/new summary
  - Grep/Glob: `Allow <tool> to search: <pattern>`
  - WebFetch: `Allow WebFetch to access: <url>`
  - WebSearch: `Allow WebSearch to query: <query>`
  - Agent: `Allow Agent to run: <agent_type>`
  - Default: `Allow <tool_name> to run`
- 5 options (see 3.3)
- Navigation hint: `Esc to cancel · Tab to amend`

**Files:**
- `src/ui/FtxuiRender.cpp` — change permission rendering from inline to overlay
- `src/ui/FtxuiPermission.cpp` — add tool-specific description generation
- `include/claude/ui/FtxuiRepl.hpp` — add permission description fields

### 3.5 Add parallel task progress display

**Problem:** No dedicated UI for sub-agent/parallel task progress. Background tasks are reported as periodic SystemInfo messages. Official shows agent tree with progress in the content area.

**Change:** When sub-agents are running, display an `AgentProgressLine` in the content area (as part of the spinner/streaming display, not in the bottom footer). Format matches TS:

```
├── [AgentType] (description) · N tool uses · M tokens
│  ⏻  Initializing... / Done
```

This is rendered as a `DisplayMessage::agentProgress` type in the message stream. The refresh thread already polls `UnifiedTaskStore` — change it to update a dedicated agent progress display instead of posting SystemInfo messages.

**Files:**
- `include/claude/ui/UiMessageTypes.hpp` — add `AgentProgress` display message type
- `src/ui/FtxuiStreaming.cpp` — change refresh thread to update agent progress display
- `src/ui/FtxuiRender.cpp` — render agent progress tree

### 3.6 Add authentication status indicator

**Problem:** No visual indicator of authentication/login status. Official shows auth status in the Notifications component (footer right side).

**Change:** Add an auth status indicator to the `PromptInputFooter` right side. Display:
- `✓` (MacMint) + dim "logged in" when API key is set and valid
- `⚠` (MacGold) + "not authenticated" when no API key
- Check auth status from `AppState::instance().apiKeyFromFd()` or `sessionIngressToken()`

**Files:**
- `src/ui/FtxuiRender.cpp` — add auth indicator to footer
- `include/claude/ui/FtxuiRepl.hpp` — add auth state field

### 3.7 Add keyboard hints

**Problem:** No keyboard shortcut hints shown in the footer. Official shows `? for shortcuts · esc to interrupt`.

**Change:** Add keyboard hints to the footer, displayed in dim text. Context-dependent:
- Idle: `? for shortcuts`
- Streaming: `esc to interrupt`
- Permission prompt: (shown in the permission overlay itself)

**Files:**
- `src/ui/FtxuiRender.cpp` — add hints to footer

---

## Part 4: Tool Activity Description (P1)

### 4.1 Replace raw JSON args with human-readable descriptions

**Problem:** Tool calls display raw JSON input like `{"file_path":"src/main.ts"}` instead of human-readable activity descriptions like "Reading src/main.ts".

**Change:** Create an `ActivityDescription` function that converts tool name + JSON input to a human-readable string. This is the most impactful change for reducing "chaotic output" perception.

Format matches TS `getActivityDescription()`:

| Tool | Input | Activity Description |
|------|-------|---------------------|
| Read | `{"file_path":"src/main.ts"}` | `Reading src/main.ts` |
| Read | `{"file_path":"src/main.ts","offset":10,"limit":20}` | `Reading src/main.ts:10-30` |
| Write | `{"file_path":"src/main.ts"}` | `Writing src/main.ts` |
| Edit | `{"file_path":"src/main.ts"}` | `Editing src/main.ts` |
| Bash | `{"command":"npm test"}` | `Running npm test` |
| Grep | `{"pattern":"TODO"}` | `Searching for "TODO"` |
| Grep | `{"pattern":"TODO","path":"src/"}` | `Searching for "TODO" in src/` |
| Glob | `{"pattern":"*.ts"}` | `Finding *.ts` |
| WebFetch | `{"url":"https://example.com"}` | `Fetching https://example.com` |
| WebSearch | `{"query":"rust tutorials"}` | `Searching "rust tutorials"` |
| Agent | `{"agent_type":"Explore"}` | `Running Explore agent` |
| LSP | `{"file_path":"src/main.ts"}` | `LSP src/main.ts` |
| MCP | `{"tool_name":"weather"}` | `Calling MCP weather` |

Tense-aware: active ("Reading") during execution, past ("Read") on completion.

Apply to all rendering contexts: FTXUI message area, spinner tool context, readline ToolStatusRenderer, collapsed tool summaries.

**Files:**
- Create: `include/claude/console/ActivityDescription.hpp` — `String getActivityDescription(const String& toolName, const Json& input, bool active = true)`
- Create: `src/console/ActivityDescription.cpp` — implementation
- `src/ui/FtxuiRender.cpp` — use ActivityDescription in tool call rendering
- `src/console/Spinner.cpp` — use in spinner tool context
- `src/console/ToolStatusRenderer.cpp` — use in tool start/result rendering
- `src/console/CollapsedToolRenderer.cpp` — use in collapsed summaries

### 4.2 Fix collapsed tool summaries

**Problem:** Collapsed groups show "[3 tool uses]" instead of "Read 3 files, Searched 2 patterns".

**Change:** The collapsed summary should enumerate tool actions by type, matching TS format:
- "Read 3 files, Searched 2 patterns"
- "Reading 3 files, Searching 2 patterns..." (active tense)

**Files:**
- `src/console/CollapsedToolRenderer.cpp` — change summary format
- `src/ui/FtxuiRender.cpp` — update FTXUI collapsed rendering

---

## Part 5: Content Area Polish (P2)

### 5.1 Fix "unknown" display in input area

**Problem:** The area below the input line shows "unknown" when model info is not set.

**Change:** Remove the standalone "unknown" text. Model info should only appear in the header and footer, never as a standalone line.

**Files:**
- `src/ui/FtxuiRender.cpp` — remove "unknown" rendering

### 5.2 Add cwd display to header

**Problem:** Header doesn't show the current working directory. Official shows cwd and git branch.

**Change:** Add cwd and optional git branch to the header, right-aligned or after model info. Format: `~/project` or `~/project (main)`. Get cwd from `AppState::instance().cwd()`, git branch from `GitContext`.

**Files:**
- `src/ui/FtxuiRender.cpp` — add cwd to header
- `include/claude/ui/FtxuiRepl.hpp` — add cwd/git state fields

---

## Implementation Order

1. **Part 1 (P0)** — Infrastructure fixes first (banner removal, default mode, spdlog, isatty, model name). These are independent and can be done in parallel.
2. **Part 4 (P1)** — Tool activity descriptions. This has the biggest user-visible impact and is independent of the layout changes.
3. **Part 3 (P1)** — Bottom area alignment. This is the core structural change and depends on Part 1 being done.
4. **Part 2 (P1)** — Header bar enhancement. Can be done in parallel with Part 3.
5. **Part 5 (P2)** — Content area polish. Depends on Part 3 layout being stable.

## Non-Goals

- **Multi-line input** (Shift+Enter) — significant input system rewrite, defer to future spec
- **Command history navigation** (Up/Down arrow) — readline already supports this; FTXUI input component needs separate work
- **Custom StatusLine via shell command** — already implemented, no changes needed
- **Voice input support** — not applicable to C++ CLI
- **Companion sprite / BUDDY** — future feature, not part of core UI alignment
- **CoordinatorTaskPanel** (ant-only feature) — not applicable
