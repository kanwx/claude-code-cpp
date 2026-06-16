# Message Pipeline Alignment Design

## 背景

本项目是 Claude Code 的 C++ 重写版，已在功能上达到 97% 完成度（82/82 命令、全部核心工具、API 客户端、权限系统）。但**输出质量和 UI 呈现**方面与原版 TypeScript 存在显著差距，导致用户体验不同。本文档设计了一套消息归一化管道（Message Pipeline），从根本上解决这些差距。

## 1. 架构

### 数据流

```
API Response
  → TypedStreamEvent
    → StreamBuffer
      → DisplayEvent
        → FtxuiRepl (incremental: 直接 push to contentBlocks_)
            ↓
        在 AnswerEnd 或 LightIncremental 窗口内:
          → MessagePipeline (替换原 AnswerPostProcessor)
              → ContentBlock[] (归一化后)
                → FtxuiRepl (全量替换，重渲染)
```

### 改动范围

- **StreamBuffer**: 移除 `AnswerPostProcessor` 的引用，改为直接 push `DisplayEvent` 到 UI
- **AnswerPostProcessor**: **删除**。其 3 个简单步骤被 `MessagePipeline` 的 7+ pass 吸收
- **MessagePipeline**: 大幅扩展，新增 groupToolResultPairs、collapseReadSearchGroups、buildLookups 等 pass
- **FtxuiRepl**: `runMessagePipeline()` 从使用 `AnswerPostProcessor` 改为调用 `MessagePipeline::process()`

### 增量策略

```cpp
enum class IncrementalMode {
    BatchOnly,        // 当前行为：AnswerEnd 一次性处理（存在长轮次中视觉抖动）
    LightIncremental, // 推荐：新进块只在前后 N 块窗口内跑局部重排序/分组
    FullIncremental   // 最大复杂度：每次新内容都跑全管道，diff 替换
};
```

**推荐**：`LightIncremental`（窗口大小 10 个块）：
- `ToolResult`/`ToolProgress` 到达时 → 在该块前后 10 个范围内跑 `reorderToolTrails` + `applyGrouping`
- `AnswerText` 到达时 → 跳过 grouping（文本是 group breaker）
- `StreamEnd` 时 → 跑**完整管道**做最终修正
- 超出窗口的已稳定块（如上一轮的 `CollapsedGroup`）不再修改，避免视觉抖动

## 2. 管道 Pass（10个，按执行顺序）

### Pass 1: `cleanThinkingTags`（保留，从 StreamBuffer 移入管道）
- 从文本中剥离 `<thinking>` / ` 飕刃0...` 等模型自生成的思考标签
- 已在 `StreamBuffer` 中运行，保持不变

### Pass 2: `reorderToolTrails`（扩展）
- 将 `ToolProgress` 放在其 `ToolResult` 之前
- 丢弃**无 matched result 的孤儿 `ToolProgress`**
- 新增：处理 interleaved tool+text ordering

### Pass 3: `groupConsecutiveToolUses`（新增）
- 同一 assistant message 中连续 `tool_use` 合并为 `GroupedToolUse`
- 记录 `toolUseIds` 列表供后续引用

### Pass 4: `groupToolResultPairs`（新增，核心）
- 配对 `tool_use` 和 `tool_result`（通过 `toolCallId`）
- 未配对的 `tool_result` 标记为合成结果

### Pass 5: `collapseReadSearchGroups`（新增，核心）
- 使用 `GroupAccumulator` 折叠连续 `Read`/`Grep`/`Glob`/`LS`/`Bash`/`WebSearch`/`WebFetch`
- 支持 `hasContentAfter` 检测 → 动词时态变化（"Read" vs "Reading"）
- `readFilePaths` 去重 → 显示 "Read 3 files" 而非 "Read 5 operations"

### Pass 6: `collapseBackgroundBash`（stub → 实现）
- 折叠后台 bash start/end 通知对
- 仅在终端 inactive 时折叠

### Pass 7: `collapseHookSummaries`（stub → 实现）
- 合并连续 `StopHookSummary` 系统消息

### Pass 8: `collapseTeammateShutdowns`（stub → 实现）
- 折叠子 agent / teammate 终止序列

### Pass 9: `buildLookups`（新增）
- 建立 `MessageLookups` 中的 O(1) 查询表，供渲染层使用

### Pass 10: `toContentBlocks`（新增，最终转换）
- 将处理后的中间表示转为 `ContentBlock[]` 渲染树

## 3. LightIncremental 增量规则

```
新块到达时:
  1. 找到该块在 contentBlocks_ 中的位置
  2. 提取 [max(0, pos-WINDOW), min(size, pos+WINDOW)] 区间
  3. 在该子范围上运行:
     a. reorderToolTrails (保证 progress→result 顺序)
     b. groupConsecutiveToolUses (同类型工具分组)
     c. 不运行 collapseReadSearchGroups（可能延迟到 StreamEnd）
  4. 将结果写回原位置
  5. 触发 FtxuiRepl 的重渲染

StreamEnd 时:
  1. 对**完整** contentBlocks_ 运行**全部 10 个 pass**
  2. 替换全部
  3. 触发重渲染
```

**窗口大小 10 的理由**：
- 一个典型的密集操作轮次最多连续 8-10 个工具调用（读文件+搜索+运行命令）
- 超过 10 的极长工具链极少见
- 窗口小，性能开销可忽略（O(20*passes) ≈ 数百次操作）

## 4. 错误处理

| 边界情况 | 处理策略 |
|----------|----------|
| 增量 vs batch 不一致 | `LightIncremental` 只修改窗口内块；超出已稳定的 fold 不重新打开。延迟到 `StreamEnd` 做最终全量修正 |
| 工具取消 | 若取消的 `ToolResult` 已在 `CollapsedGroup` 内 → 将该组拆回独立块。`StreamEnd` 时全量 re-group |
| 孤儿 `ToolProgress` | `reorderToolTrails` 丢弃无对应 result 的 progress（符合现有行为）|
| 块数溢出 | `MAX_BLOCKS = 2000`，超出旧块降级为 `Tombstone` |
| 并发安全 | `MessagePipeline` 纯函数，无副作用。调用端加锁 |

## 5. 测试

- **单位测试**：每个 pass 的输入/输出对照表（现有 `MessagePipeline` 的各 pass 为 `public`）
- **集成测试**：`process()` 端到端，验证与 TypeScript 行为描述一致
- **增量等价测试**：`LightIncremental` 最终输出与 `BatchOnly` 完全一致
- **边界测试**：空输入、单块、全同类型、交错、孤儿、工具取消

## 6. 优先级

| 优先级 | 任务 | 投入 | 用户可见度 |
|--------|------|------|------------|
| **P0** | 落地 `LightIncremental` 增量模式 | 高 | 中（消除长轮次视觉抖动） |
| **P0** | 填充 `CollapsedReadSearchGroups`（已 70% 完成） | 中 | **高**（read-heavy 场景体验大幅改善） |
| **P1** | per-tool 结果格式化（Bash/Read/Write/Grep 等） | 中 | **高**（最直观的 TS 差距） |
| **P1** | 统一 `⎿` / `●` 前缀系统 | 低 | **高**（视觉统一性） |
| **P1** | 实现 `collapseBackgroundBash` / `collapseHookSummaries` | 中 | 低 |
| **P2** | 系统消息子类型（CompactBoundary、TurnDuration inline 等） | 中 | 中 |
| **P2** | 用户输入多样性（按 /、! 等前缀区分显示） | 低 | 低 |

## 7. 关键文件改动

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/stream/AnswerPostProcessor.cpp` | **删除** | 被 MessagePipeline 取代 |
| `include/claude/stream/AnswerPostProcessor.hpp` | **删除** | —— |
| `src/stream/MessagePipeline.cpp` | 扩展 | 新增 pass 3-9 实现 |
| `include/claude/stream/MessagePipeline.hpp` | 扩展 | 新增 pass 接口 |
| `src/stream/StreamBuffer.cpp` | 修改 | 移除 AnswerPostProcessor 引用 |
| `src/ui/FtxuiRepl.cpp` | 修改 | `runMessagePipeline()` 调用新接口 |
| `src/ui/MessagePipeline.cpp` | 修改 | UI 层集成点（已存在但可能需改动） |

# 8. 备注：与 TypeScript 的差距对照

| 差距编号（gap-analysis） | 本设计对应 | 状态 |
|--------------------------|------------|------|
| Gap #1：Message Type System | `UIMessage` 中间层 + `ContentBlock` 扩展（已存在） | 部分完成 |
| Gap #2：Pipeline (7+ passes) | 本文档完整设计 | 70% 实现，需增量模式 + 完善 |
| Gap #3：Tool Result 格式化 | P1 快赢任务，需要渲染层配合，不在本文档范围 | 待实现 |
| Gap #4：Collapsed Read/Search | `groupToolResultPairs` + `collapseReadSearchGroups` | 已完整设计 |
| Gap #5：Compact Boundary | 需要扩展 SystemMessage 子类型支持 | 部分已有 |
| Gap #6：System Messages | 已在 ContentBlock 中保留 SystemMessage 子类型 | 需渲染层实现 |
| Gap #7：Margin System（`⎿`/`●`） | P1 快赢任务，渲染层改动，不在本文档范围 | 待实现 |
| Gap #8：Streaming Tool Use | 已部分存在（`ToolProgress`） | 基础已有 |
| Gap #10：Transcript/Fullscreen | 不在本文档范围 | 低优先级 |

# 9. 结论

MessagePipeline 是 C++ 版本输出质量追赶原版 TypeScript 的**基础设施核心**。当前已具备 70% 的实现骨架（`reorderToolTrails`、`applyGrouping`、`collapseReadSearchGroups`、`buildLookups` 已实现），剩余差距是：

1. **增量运行模式**（`LightIncremental`）：消除长轮次中的视觉抖动
2. **3 个 stub 的填充**（`collapseBackgroundBash`、`collapseHookSummaries`、`collapseTeammateShutdowns`）
3. **工具结果格式化层**：依托 MessagePipeline 的 `buildLookups` + 渲染层配合
4. **统一前缀系统**：与 ToolResult 格式化同步实现

实施后预计可缩小 80% 的输出质量差距。
