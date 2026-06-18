#!/bin/bash
# Analyze metrics JSONL output.
# Usage: ./analyze_jsonl.sh /path/to/metrics.jsonl [label]
set -euo pipefail

JSONL="${1:-/tmp/metrics.jsonl}"
LABEL="${2:-metrics}"

if ! command -v jq &>/dev/null; then
    echo "ERROR: jq is required. Install with: brew install jq"
    exit 1
fi

if [ ! -f "$JSONL" ]; then
    echo "ERROR: file not found: $JSONL"
    exit 1
fi

LINES=$(wc -l < "$JSONL" | tr -d ' ')
echo "============================================================"
echo " Metrics Analysis: $LABEL"
echo " File: $JSONL"
echo " Lines: $LINES"
echo "============================================================"

# --- Per-Turn Breakdown ---
echo ""
echo "--- Per-Turn Breakdown ---"
echo ""

jq -r '
  def safe(n): if n then (n | tostring) else "N/A" end;

  # Handle old schema where meta_patterns_inter_tool / meta_patterns_final_answer may be missing
  def mi: .meta_patterns_inter_tool // .meta_patterns // {};
  def mf: .meta_patterns_final_answer // {total: 0, total_en: 0, total_zh: 0};
  def mp: .meta_patterns // {total: 0, total_en: 0, total_zh: 0};
  def m2v: .inter_tool_words.values // [];

  "[" + (.user_turn_index // "?" | tostring) + "] " +
  "snapshot=" + (.snapshot_index // "?" | tostring) + " " +
  "prompt=" + (.prompt_version // "?") + " " +
  "model=" + (.model // "?") + " " +
  "scope=" + (.metric_scope // "?") + " " +
  "api_round=" + (.api_round_index // 0 | tostring) + " " +
  "silent=" + (.is_silent_round | tostring) + "\n" +
  "  M1 total:   " + safe(mp.total) + "  (en=" + safe(mp.total_en) + " zh=" + safe(mp.total_zh) + ")\n" +
  "  M1 inter:   " + safe(mi.total) + "  (en=" + safe(mi.total_en) + " zh=" + safe(mi.total_zh) + ") ← KEY\n" +
  "  M1 final:   " + safe(mf.total) + "  (en=" + safe(mf.total_en) + " zh=" + safe(mf.total_zh) + ")\n" +
  "  M2: entries=" + safe(m2v | length) +
      " sum=" + safe(m2v | add // 0) +
      " median=" + safe(.inter_tool_words.median // "?") + "\n" +
  "  Blocks: answerText=" + safe(.block_counts.answer_text) +
      " toolResult=" + safe(.block_counts.tool_result) +
      " toolGroup=" + safe(.block_counts.tool_group) +
      " collapsedGroup=" + safe(.block_counts.collapsed_group) + " agentProgress=" + safe(.block_counts.agent_progress) + "\n" +
  "  toolCalls=" + safe(.tool_call_count) +
      " agentProgress=" + safe(.agent_progress_count // 0) +
      " toolLike=" + safe(.tool_like_block_count // 0) +
      " words=" + safe(.total_user_words) +
      " Reading1files=" + (
        if .combined_text and (.combined_text | length) > 0 then
          (.combined_text | split("Reading 1 file") | length - 1)
        else 0 end | tostring
      ) + "\n" +
  ""
' "$JSONL"

# --- Aggregate Summary ---
echo ""
echo "--- Aggregate Summary ---"
echo ""

# ===================================================================
# Final Turn Aggregate — uses only the LAST cumulative snapshot
# (highest api_round_index) per user turn. This is the CORRECT
# aggregate for drawing experimental conclusions, because each
# cumulative snapshot already includes all data from earlier
# api_rounds within the same turn. Summing all snapshots would
# double-count tool calls, blocks, etc.
# ===================================================================
echo ""
echo "=== FINAL TURN AGGREGATE (last snapshot per turn — USE FOR CONCLUSIONS) ==="
echo ""

jq -s -r '
  def mp: .meta_patterns // {total: 0, total_en: 0, total_zh: 0};
  def mi: .meta_patterns_inter_tool // mp;
  def mf: .meta_patterns_final_answer // {total: 0, total_en: 0, total_zh: 0};
  def m2v: .inter_tool_words.values // [];

  # Group by user_turn_index, keep only the LAST (highest api_round_index) per turn.
  # Fallback to .turn_index for old JSONL without user_turn_index.
  (group_by(.user_turn_index // .turn_index) | map(sort_by(.api_round_index // 0) | last)) as $turns |

  {
    total_snapshots: length,
    turns: ($turns | length),
    prompts: [$turns[] | .prompt_version // "?"] | unique,
    models:  [$turns[] | .model // "?"] | unique,
    silent:  [$turns[] | select(.is_silent_round == true)] | length,
    m1_total_en:    [$turns[] | mp.total_en] | add,
    m1_total_zh:    [$turns[] | mp.total_zh] | add,
    m1_total:       [$turns[] | mp.total] | add,
    m1_inter_en:    [$turns[] | mi.total_en] | add,
    m1_inter_zh:    [$turns[] | mi.total_zh] | add,
    m1_inter_total: [$turns[] | mi.total] | add,
    m1_final_en:    [$turns[] | mf.total_en] | add,
    m1_final_zh:    [$turns[] | mf.total_zh] | add,
    m1_final_total: [$turns[] | mf.total] | add,
    m2_all:         [$turns[] | m2v[]?] | sort,
    m2_entries:     [$turns[] | m2v | length] | add,
    m2c_all:        [$turns[] | (.inter_tool_chars.values // [])[]?] | sort,
    m2c_entries:    [$turns[] | (.inter_tool_chars.values // []) | length] | add,
    at_blocks:      [$turns[] | .block_counts.answer_text] | add,
    tr_blocks:      [$turns[] | .block_counts.tool_result] | add,
    tg_blocks:      [$turns[] | .block_counts.tool_group] | add,
    cg_blocks:      [$turns[] | .block_counts.collapsed_group] | add,
    ag_blocks:      [$turns[] | .block_counts.agent_progress // 0] | add,
    tool_calls:     [$turns[] | .tool_call_count] | add,
    agent_count:    [$turns[] | .agent_progress_count // 0] | add,
    tool_like:      [$turns[] | .tool_like_block_count // 0] | add,
    total_words:    [$turns[] | .total_user_words] | add,
    reading1_count: [$turns[] |
      (if .combined_text and (.combined_text | length) > 0 then (.combined_text | split("Reading 1 file") | length - 1) else 0 end)
    ] | add
  }
  |
  "Total snapshots:  \(.total_snapshots)\n" +
  "Unique turns:     \(.turns)  (silent: \(.silent))\n" +
  "Prompt variants:  \(.prompts | join(", "))\n" +
  "Models:           \(.models | join(", "))\n" +
  "\n" +
  "=== M1: Meta Patterns ===\n" +
  "  total_en:       \(.m1_total_en)\n" +
  "  total_zh:       \(.m1_total_zh)\n" +
  "  total:          \(.m1_total)\n" +
  "  inter-tool_en:  \(.m1_inter_en)  ← KEY: should be low for efficiency prompt\n" +
  "  inter-tool_zh:  \(.m1_inter_zh)  ← KEY: should be low for efficiency prompt\n" +
  "  inter-tool:     \(.m1_inter_total)\n" +
  "  final_en:       \(.m1_final_en)  (in final answers — OK)\n" +
  "  final_zh:       \(.m1_final_zh)  (in final answers — OK)\n" +
  "  final:          \(.m1_final_total)\n" +
  "\n" +
  "=== M2: Inter-Tool Word Counts ===\n" +
  "  entries:        \(.m2_entries)\n" +
  "  sum:            \(.m2_all | add // 0)\n" +
  (
    if (.m2_all | length) > 0 then
      "  median:         \(.m2_all[.m2_all | length / 2 | floor])\n" +
      "  p25:            \(.m2_all[.m2_all | length * 0.25 | floor])\n" +
      "  p75:            \(.m2_all[.m2_all | length * 0.75 | floor])\n" +
      "  p90:            \(.m2_all[.m2_all | length * 0.90 | floor])\n" +
      "  max:            \(.m2_all[-1])\n"
    else
      ""
    end
  ) +
  "\n" +
  "=== M2c: Inter-Tool Char Counts (CJK accuracy) ===\n" +
  "  entries:        \(.m2c_entries)\n" +
  "  sum:            \(.m2c_all | add // 0)\n" +
  (
    if (.m2c_all | length) > 0 then
      "  median:         \(.m2c_all[.m2c_all | length / 2 | floor])\n" +
      "  p25:            \(.m2c_all[.m2c_all | length * 0.25 | floor])\n" +
      "  p75:            \(.m2c_all[.m2c_all | length * 0.75 | floor])\n" +
      "  p90:            \(.m2c_all[.m2c_all | length * 0.90 | floor])\n" +
      "  max:            \(.m2c_all[-1])\n"
    else
      ""
    end
  ) +
  "\n" +
"=== M3: Block Counts ===\n" +
  "  AnswerText:     \(.at_blocks)\n" +
  "  ToolResult:     \(.tr_blocks)\n" +
  "  ToolGroup:      \(.tg_blocks)\n" +
  "  AgentProgress:  \(.ag_blocks)\n" +
  "  CollapsedGroup: \(.cg_blocks)\n" +
  "\n" +
  "=== Summary ===\n" +
  "  toolCalls:      \(.tool_calls)\n" +
  "  agentCount:     \(.agent_count)\n" +
  "  toolLike:       \(.tool_like)\n" +
  "  totalWords:     \(.total_words)\n" +
  "  Reading 1 file: \(.reading1_count)  ← KEY: should decrease for efficiency prompt\n"
' "$JSONL"

# ===================================================================
# Snapshot Aggregate — sums ALL snapshots (for debugging only).
# Numbers here WILL be inflated by cumulative double-counting.
# Do NOT use for experimental conclusions.
# ===================================================================
echo ""
echo "=== SNAPSHOT AGGREGATE (all snapshots summed — DEBUG ONLY, inflated) ==="
echo ""

jq -s -r '
  def mp: .meta_patterns // {total: 0, total_en: 0, total_zh: 0};
  def mi: .meta_patterns_inter_tool // mp;
  def mf: .meta_patterns_final_answer // {total: 0, total_en: 0, total_zh: 0};
  def m2v: .inter_tool_words.values // [];

  {
    snapshots: length,
    prompts: [.[] | .prompt_version // "?"] | unique,
    models:  [.[] | .model // "?"] | unique,
    m1_total_en:    [.[] | mp.total_en] | add,
    m1_total_zh:    [.[] | mp.total_zh] | add,
    m1_total:       [.[] | mp.total] | add,
    m1_inter_en:    [.[] | mi.total_en] | add,
    m1_inter_zh:    [.[] | mi.total_zh] | add,
    m1_inter_total: [.[] | mi.total] | add,
    m1_final_en:    [.[] | mf.total_en] | add,
    m1_final_zh:    [.[] | mf.total_zh] | add,
    m1_final_total: [.[] | mf.total] | add,
    m2_all:         [.[] | m2v[]?] | sort,
    m2_entries:     [.[] | m2v | length] | add,
    m2c_all:        [.[] | (.inter_tool_chars.values // [])[]?] | sort,
    m2c_entries:    [.[] | (.inter_tool_chars.values // []) | length] | add,
    at_blocks:      [.[] | .block_counts.answer_text] | add,
    tr_blocks:      [.[] | .block_counts.tool_result] | add,
    tg_blocks:      [.[] | .block_counts.tool_group] | add,
    cg_blocks:      [.[] | .block_counts.collapsed_group] | add,
    ag_blocks:      [.[] | .block_counts.agent_progress // 0] | add,
    tool_calls:     [.[] | .tool_call_count] | add,
    agent_count:    [.[] | .agent_progress_count // 0] | add,
    tool_like:      [.[] | .tool_like_block_count // 0] | add,
    total_words:    [.[] | .total_user_words] | add,
    reading1_count: [.[] |
      (if .combined_text and (.combined_text | length) > 0 then (.combined_text | split("Reading 1 file") | length - 1) else 0 end)
    ] | add
  }
  |
  "Snapshots:        \(.snapshots)\n" +
  "Prompt variants:  \(.prompts | join(", "))\n" +
  "Models:           \(.models | join(", "))\n" +
  "\n" +
  "=== M1: Meta Patterns (INFLATED) ===\n" +
  "  total:          \(.m1_total)\n" +
  "  inter-tool:     \(.m1_inter_total)  ← INFLATED by cumulative snapshots\n" +
  "  final:          \(.m1_final_total)\n" +
  "\n" +
  "=== M2: Inter-Tool Word Counts (INFLATED) ===\n" +
  "  entries:        \(.m2_entries)\n" +
  "  sum:            \(.m2_all | add // 0)\n" +
  (
    if (.m2_all | length) > 0 then
      "  median:         \(.m2_all[.m2_all | length / 2 | floor])\n" +
      "  p25:            \(.m2_all[.m2_all | length * 0.25 | floor])\n" +
      "  p75:            \(.m2_all[.m2_all | length * 0.75 | floor])\n" +
      "  p90:            \(.m2_all[.m2_all | length * 0.90 | floor])\n" +
      "  max:            \(.m2_all[-1])\n"
    else
      ""
    end
  ) +
  "\n" +
  "=== M2c: Inter-Tool Char Counts (INFLATED) ===\n" +
  "  entries:        \(.m2c_entries)\n" +
  "  sum:            \(.m2c_all | add // 0)\n" +
  (
    if (.m2c_all | length) > 0 then
      "  median:         \(.m2c_all[.m2c_all | length / 2 | floor])\n" +
      "  p25:            \(.m2c_all[.m2c_all | length * 0.25 | floor])\n" +
      "  p75:            \(.m2c_all[.m2c_all | length * 0.75 | floor])\n" +
      "  p90:            \(.m2c_all[.m2c_all | length * 0.90 | floor])\n" +
      "  max:            \(.m2c_all[-1])\n"
    else
      ""
    end
  ) +
  "\n" +
"=== M3: Block Counts (INFLATED) ===\n" +
  "  AnswerText:     \(.at_blocks)\n" +
  "  ToolResult:     \(.tr_blocks)\n" +
  "  ToolGroup:      \(.tg_blocks)\n" +
  "  AgentProgress:  \(.ag_blocks)\n" +
  "  CollapsedGroup: \(.cg_blocks)\n" +
  "\n" +
  "=== Summary (INFLATED) ===\n" +
  "  toolCalls:      \(.tool_calls)\n" +
  "  agentCount:     \(.agent_count)\n" +
  "  toolLike:       \(.tool_like)\n" +
  "  totalWords:     \(.total_words)\n" +
  "  Reading 1 file: \(.reading1_count)\n"
' "$JSONL"

# --- Alternating Structure Detection ---
echo ""
echo "--- Alternating Structure Detection ---"
echo "  (AnswerText → ToolBlock → AnswerText → ToolBlock pattern)"
echo ""

jq -r '
  .combined_text as $t |
  "[turn " + (.user_turn_index // .turn_index | tostring) + "] " +
  if ($t and ($t | length) > 0) then
    "combined_text length: \($t | length) chars\n" +
    "  \"---\" boundaries (block transitions): " +
    (($t | split("\n---\n") | length - 1) | tostring) + "\n"
  else
    "combined_text empty (silent turn)\n"
  end
' "$JSONL"

# --- English Meta Pattern Samples (inter-tool only) ---
echo ""
echo "--- English Meta Pattern Samples (inter-tool) ---"
echo ""

jq -r '
  def mi: .meta_patterns_inter_tool // {};
  def en: (mi.total_en // 0);
  select(en > 0) |
  "  [turn " + (.user_turn_index // .turn_index | tostring) + "] en_inter=" + (en | tostring) +
    " let_me=" + ((mi.let_me // 0) | tostring) +
    " ill=" + ((mi.ill // 0) | tostring) +
    " first=" + ((mi.first // 0) | tostring) +
    " now=" + ((mi.now // 0) | tostring) +
    " next=" + ((mi.next // 0) | tostring) +
    " need_should=" + ((mi.need_should // 0) | tostring) +
    " lets=" + ((mi.lets // 0) | tostring) + "\n" +
    "    samples: " + ((.text_samples // []) | join(" | ") | .[0:300]) + "\n"
' "$JSONL" | head -40

# --- Chinese Meta Pattern Samples (inter-tool only) ---
echo ""
echo "--- Chinese Meta Pattern Samples (inter-tool) ---"
echo ""

jq -r '
  def mi: .meta_patterns_inter_tool // {};
  def zh: (mi.total_zh // 0);
  select(zh > 0) |
  "  [turn " + (.user_turn_index // .turn_index | tostring) + "] zh_inter=" + (zh | tostring) +
    " rang_wo=" + ((mi.rang_wo // 0) | tostring) +
    " wo_lai=" + ((mi.wo_lai // 0) | tostring) +
    " jixu=" + ((mi.jixu_duqu // 0) | tostring) +
    " jie_xia=" + ((mi.jie_xia_lai // 0) | tostring) +
    " xianzai=" + ((mi.xianzai_rangwo // 0) | tostring) + "\n" +
    "    samples: " + ((.text_samples // []) | join(" | ") | .[0:300]) + "\n"
' "$JSONL" | head -40

# --- Inter-Tool Text Samples (final turn snapshots only, deduped) ---
echo ""
echo "--- Inter-Tool Text Samples (final turn snapshots only, deduped) ---"
echo ""

jq -s -r '
  # Take only the LAST cumulative snapshot per user turn
  (group_by(.user_turn_index // .turn_index) | map(sort_by(.api_round_index // 0) | last))[] |
  select(.inter_tool_text_samples and (.inter_tool_text_samples | length) > 0) |
  "  [turn " + (.user_turn_index // .turn_index | tostring) + " snapshot=" + (.snapshot_index // "?" | tostring) + "] " +
  (.inter_tool_text_samples | length | tostring) + " samples:\n" +
  (
    .inter_tool_text_samples[:10] | map(
      "    wc=" + (.word_count | tostring) + " cc=" + (.char_count | tostring) +
      " prev=" + .prev_block_type +
      " next=" + .next_block_type +
      " patterns=[" + (.matched_meta_patterns | join(",")) + "]\n" +
      "      \"" + .text + "\""
    ) | join("\n")
  ) + "\n"
' "$JSONL" | head -80

echo ""
echo "--- Done ---"
