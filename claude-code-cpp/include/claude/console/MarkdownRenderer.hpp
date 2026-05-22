#pragma once

#include "../core/Types.hpp"

#include <ostream>
#include <vector>

namespace claude {

/// Markdown 渲染器 —— 匹配原版 TS 设计
/// 代码块使用 box-drawing 边框 (╭─┐│╰┘), 语言 badge, 行号
/// 支持: headers, lists, bold, italic, inline code, code blocks,
///       blockquotes, horizontal rules, links, task lists, tables
class MarkdownRenderer {
public:
    explicit MarkdownRenderer(std::ostream& out, int terminalWidth = 80);

    void render(const String& markdown);

    // 流式渲染状态
    struct StreamState {
        bool inCodeBlock = false;
        String codeLang;
        bool inBold = false;
        bool inItalic = false;
        String partialLine;         // Unfinished line from previous chunk
        int codeLineNum = 0;        // Line number counter in streaming code block
        int codeMaxLines = 0;       // For line number width calculation
        bool inTable = false;       // Currently accumulating table rows
        std::vector<std::vector<String>> tableRows; // Buffered table rows
        bool tableHasHeader = false;// Whether first row is a header

        // Block-boundary optimization
        String accumulated;                    // All text received so far
        size_t stablePrefixEnd = 0;           // Position of last confirmed block boundary
        std::vector<String> cachedPrefixLines; // Pre-rendered lines for the stable prefix
    };

    void renderStream(const String& chunk, StreamState& state);
    void finishStream(StreamState& state);

    /// Set terminal width for proper border sizing
    void setTerminalWidth(int width) { terminalWidth_ = width; }

private:
    void renderLine(const String& line);
    void renderCodeBlock(const String& code, const String& lang);
    void renderBlockquote(const String& line, int level = 1);
    void renderHorizontalRule();
    void renderTaskList(const String& line);
    void renderDiffBlock(const String& code);
    void renderTable(const std::vector<std::vector<String>>& rows, bool hasHeader);

    /// Apply inline formatting (bold, italic, code, links)
    String applyInlineFormatting(const String& text);

    /// Compute indentation level for nested lists
    int listIndentLevel(const String& line) const;

    /// Parse table row from pipe-delimited line
    std::vector<String> parseTableRow(const String& line) const;

    /// Check if line is a table separator (---|---|---)
    bool isTableSeparator(const String& line) const;

    /// Pad/truncate a cell to fit column width
    static String padCell(const String& cell, int width, bool alignRight = false);

    /// Compute line number width for given max line count
    static int lineNumberWidth(int maxLines);

    std::ostream& out_;
    int terminalWidth_;

    // Table accumulation state for non-streaming render
    bool inTable_ = false;
    std::vector<std::vector<String>> tableRows_;
    bool tableHasHeader_ = false;
};

} // namespace claude
