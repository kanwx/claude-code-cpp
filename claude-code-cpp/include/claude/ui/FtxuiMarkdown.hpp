#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace claude {

/// Markdown → FTXUI Element renderer
/// Supports: code blocks, inline code, bold, italic, headers, bullet lists,
/// numbered lists, blockquotes, links, horizontal rules, tables, task lists, nested lists
class FtxuiMarkdown {
public:
    /// Render a markdown string to a vector of FTXUI Elements (one per visual block)
    static std::vector<ftxui::Element> render(const std::string& markdown);

    /// Render a single inline text segment (handles bold/italic/code/links)
    static ftxui::Element renderInline(const std::string& text);

    /// Check if text contains Markdown syntax markers (fast path for plain text)
    static bool hasMarkdownSyntax(const std::string& text);

    /// Syntax-highlight a single line of code
    static std::string highlightLine(const std::string& line, const std::string& lang);

    // ========== Streaming Renderer ==========
    /// Incremental markdown parsing for streaming text.
    /// Splits growing text into "stable prefix" (cached, never re-parsed)
    /// and "unstable suffix" (re-lexed per delta).
    /// For 50KB response at 50fps: reparse ~100 bytes instead of 50KB per frame.

    class StreamingRenderer {
    public:
        StreamingRenderer() = default;

        /// Append a new text chunk to the streaming buffer.
        /// Commits completed blocks to cache, only re-parses the tail.
        void append(const std::string& chunk);

        /// Get the current rendered elements (stable + unstable suffix).
        /// Called each frame during streaming.
        std::vector<ftxui::Element> render();

        /// Finalize: close any open blocks, return final render.
        /// Call when streaming ends.
        std::vector<ftxui::Element> finalize();

        /// Reset to initial state for a new stream.
        void reset();

        /// Get the full accumulated text so far.
        const std::string& fullText() const { return fullText_; }

    private:
        /// Find the boundary between stable and unstable text.
        /// The boundary is at the last newline that precedes a completed block.
        size_t findStableBoundary() const;

        /// Parse and cache blocks from stablePrefixEnd_ to the new boundary.
        void commitStableBlocks(size_t newBoundary);

        // Full accumulated text
        std::string fullText_;

        // Cached rendered elements for the stable prefix
        std::vector<ftxui::Element> completedElements_;

        // Offset in fullText_ where the stable prefix ends
        size_t stablePrefixEnd_ = 0;

        // The unstable suffix text (from stablePrefixEnd_ to end)
        // Re-parsed every frame during streaming
        std::string unstableSuffix_;
    };

private:
    struct CodeBlock {
        std::string lang;
        std::vector<std::string> lines;
    };

    struct ParsedBlock {
        enum Type { Paragraph, CodeBlockType, Header, BulletList, NumberedList,
                    Blockquote, HRule, Table, TaskList, NestedList };
        Type type;
        int level = 0;           // header level (1-6)
        std::string text;        // paragraph/header/bq text
        CodeBlock code;          // code block content
        std::vector<std::string> items;  // list items
        // Table fields
        std::vector<std::string> headerCells;
        std::vector<std::vector<std::string>> rows;
        // Task list fields
        std::vector<std::pair<bool, std::string>> taskItems; // checked, text
        // Nested list fields
        int indentLevel = 0;     // nesting depth
    };

    static std::vector<ParsedBlock> parse(const std::string& markdown);
    static ftxui::Element renderBlock(const ParsedBlock& block);
    static ftxui::Element renderCodeBlock(const CodeBlock& code);

    // Inline rendering helpers
    static std::vector<ftxui::Element> parseInlineElements(const std::string& text);
    static ftxui::Element elementsToParagraph(std::vector<ftxui::Element>&& elems);

    // Keyword lists for syntax highlighting
    static const std::vector<std::string> CPP_KEYWORDS;
    static const std::vector<std::string> PYTHON_KEYWORDS;
    static const std::vector<std::string> RUST_KEYWORDS;
    static const std::vector<std::string> GO_KEYWORDS;
    static const std::vector<std::string> JS_KEYWORDS;
    static const std::vector<std::string> BASH_KEYWORDS;
    static const std::vector<std::string> JSON_SPECIAL;

    static bool isKeyword(const std::string& word, const std::string& lang);
};

} // namespace claude
