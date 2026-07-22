# Claude Code 研究项目

本仓库包含三个围绕 Claude Code 的研究与工程项目。

## 项目总览

| 目录 | 项目 | 语言 | 说明 |
|------|------|------|------|
| [`claude-code-cpp/`](claude-code-cpp) | **Claude Code C++** | C++23 | Claude Code CLI 的 C++ 重写，追求输出体验对等 |
| [`src/`](src) | **Claude Code 源码** | TypeScript | Claude Code 原始源码快照，供架构研究与安全分析 |
| [`ontology-platform/`](ontology-platform) | **本体建模平台** | C++23 | 符号+神经混合推理系统，含 RAG 管道和 MCP 工具 |

---

## 1. Claude Code C++ 重写

C++23 + FTXUI 重写的 Claude Code CLI。当前进度：**Phase 6 — 输出体验对等**。

### 当前状态

- **分支**: `ui-polish-ftxui`
- **构建**: `cmake --build build -j$(sysctl -n hw.logicalcpu)`
- **测试**: 723+ ctest PASS
- **Phase 6 进度**: P6-P0a/P0b 已提交，P6-P0c RCA 完成

### 核心模块

| 模块 | 说明 |
|------|------|
| `AgentLoop` | 工具执行循环、流式事件管理 |
| `StreamBuffer` | 文本积累、thinking tag 剥离、DisplayEvent 发射 |
| `MessagePipeline` | AnswerEnd 7-pass 后处理（分组、折叠、重排） |
| `FtxuiRepl` | FTXUI 前端，ContentBlock 构建 + 渲染 |
| `ContentBlockFtxui` | 所有内容块类型的 FTXUI 渲染器 |

### 工具集

Bash, Read, Write, Edit, Glob, Grep, WebSearch, WebFetch, Agent (sub-agent), Task, Skill, MCP, LSP, NotebookEdit, EnterPlanMode/ExitPlanMode, EnterWorktree/ExitWorktree, CronCreate, AskUserQuestion

### Phase 6 文档

详细 RCA 和设计文档见 [`docs/superpowers/`](claude-code-cpp/docs/superpowers/)。

---

## 2. Claude Code TypeScript 源码

2026-03-31 通过 npm 包 source map 暴露的 Claude Code 原始源码。仅供**教育、防御性安全研究、软件供应链分析**使用。

- **语言**: TypeScript (Bun 运行时)
- **终端 UI**: React + Ink
- **规模**: ~1,900 文件, 512,000+ 行
- **架构要点**: 46K 行 QueryEngine、40+ 工具、50+ 命令、多 Agent 编排、MCP/LSP 协议

原始所有者: Anthropic。本仓库与 Anthropic 无关联。

---

## 3. 本体建模平台

符号推理 + 神经推理的混合智能系统，支持 SPARQL、SWRL、知识图谱嵌入（TransE/DistMult/ComplEx/RotatE），含 RAG 管道和 MCP 认知工具。

### 核心能力

- **推理**: 符号/神经/混合, SWRL 规则, 图谱嵌入
- **RAG**: 文档摄入 → 分块 → 嵌入 → 向量搜索 → 实体识别 → 图谱扩展 → 融合排序
- **MCP**: 12 个认知工具，LLM 协作
- **兼容**: HttpRagClient 接口（claude-code-cpp 可直接对接）
- **降级**: 无外部服务时可零依赖运行（hash_fingerprint + 内存搜索）

### 构建

```bash
cd ontology-platform/build && make
./ontology-server  # HTTP 8080, WebSocket 8081
```

---

## 快速链接

- [Claude Code C++ Phase 6 文档](claude-code-cpp/docs/superpowers/)
- [Claude Code TS 源码架构](src/)
- [香港民熙《合法等于正当吗》](2026-03-09-is-legal-the-same-as-legitimate-ai-reimplementation-and-the-erosion-of-copyleft.md)
- [C++ 构建指南](claude-code-cpp/BUILD.md)（如存在）
