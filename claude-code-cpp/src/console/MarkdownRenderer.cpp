#include <claude/console/MarkdownRenderer.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/MessageResponse.hpp>
#include <regex>
#include <iomanip>
#include <algorithm>

namespace claude {

MarkdownRenderer::MarkdownRenderer(std::ostream& out, int terminalWidth)
    : out_(out), terminalWidth_(terminalWidth) {}

void MarkdownRenderer::render(const String& markdown) {
    std::istringstream stream(markdown);
    String line;
    bool inCodeBlock = false;
    String codeLang;
    std::vector<String> codeLines;

    while (std::getline(stream, line)) {
        // Handle code blocks
        if (line.starts_with("```")) {
            if (!inCodeBlock) {
                inCodeBlock = true;
                codeLang = line.substr(3);
                codeLang.erase(0, codeLang.find_first_not_of(" \t"));
                codeLang.erase(codeLang.find_last_not_of(" \t") + 1);
                codeLines.clear();
            } else {
                inCodeBlock = false;
                String code;
                for (const auto& codeLine : codeLines) {
                    code += codeLine + "\n";
                }
                // Special handling for diff language
                if (codeLang == "diff") {
                    renderDiffBlock(code);
                } else {
                    renderCodeBlock(code, codeLang);
                }
            }
            continue;
        }

        if (inCodeBlock) {
            codeLines.push_back(line);
            continue;
        }

        // Table accumulation
        if (line.find('|') != String::npos && !line.starts_with(">")) {
            auto cells = parseTableRow(line);
            bool isSep = isTableSeparator(line);

            if (!inTable_) {
                // Start table if this looks like a table row
                if (!cells.empty() && (line.starts_with("|") || line.find(" | ") != String::npos)) {
                    inTable_ = true;
                    tableRows_.clear();
                    tableHasHeader_ = false;
                    if (!isSep) {
                        tableRows_.push_back(cells);
                    }
                } else {
                    renderLine(line);
                }
            } else {
                if (isSep) {
                    tableHasHeader_ = true;
                    // Skip separator line, just mark header
                } else if (!cells.empty()) {
                    tableRows_.push_back(cells);
                } else {
                    // End of table
                    renderTable(tableRows_, tableHasHeader_);
                    inTable_ = false;
                    tableRows_.clear();
                    renderLine(line);
                }
            }
            continue;
        }

        // Flush table if we hit a non-table line
        if (inTable_) {
            renderTable(tableRows_, tableHasHeader_);
            inTable_ = false;
            tableRows_.clear();
        }

        renderLine(line);
    }

    // Flush remaining table
    if (inTable_) {
        renderTable(tableRows_, tableHasHeader_);
        inTable_ = false;
        tableRows_.clear();
    }
}

int MarkdownRenderer::listIndentLevel(const String& line) const {
    int indent = 0;
    for (size_t i = 0; i < line.length(); ++i) {
        if (line[i] == ' ') indent++;
        else if (line[i] == '\t') indent += 4;
        else break;
    }
    return indent / 2;
}

void MarkdownRenderer::renderLine(const String& line) {
    if (line.empty()) {
        out_ << "\n";
        return;
    }

    // Horizontal rule: --- or *** or ___ (3+ chars, all same)
    if ((line.find_first_not_of('-') == String::npos && line.length() >= 3) ||
        (line.find_first_not_of('*') == String::npos && line.length() >= 3) ||
        (line.find_first_not_of('_') == String::npos && line.length() >= 3)) {
        renderHorizontalRule();
        return;
    }

    // Headers
    if (line.starts_with("#### ")) {
        out_ << AnsiStyle::DIM << AnsiStyle::BOLD << line.substr(5) << AnsiStyle::RESET << "\n";
        return;
    }
    if (line.starts_with("### ")) {
        out_ << AnsiStyle::BRIGHT_CYAN << AnsiStyle::BOLD << line.substr(4) << AnsiStyle::RESET << "\n";
        return;
    }
    if (line.starts_with("## ")) {
        out_ << AnsiStyle::BRIGHT_GREEN << AnsiStyle::BOLD << line.substr(3) << AnsiStyle::RESET << "\n";
        return;
    }
    if (line.starts_with("# ")) {
        out_ << AnsiStyle::BRIGHT_MAGENTA << AnsiStyle::BOLD << line.substr(2) << AnsiStyle::RESET << "\n";
        return;
    }

    // Task list: - [x] or - [ ]
    if (line.starts_with("- [x] ") || line.starts_with("- [X] ") ||
        line.starts_with("- [ ] ")) {
        renderTaskList(line);
        return;
    }

    // Blockquote: > text (with nesting support)
    {
        int bqLevel = 0;
        size_t pos = 0;
        while (pos < line.size() && line[pos] == '>') {
            bqLevel++;
            pos++;
            if (pos < line.size() && line[pos] == ' ') pos++;
        }
        if (bqLevel > 0) {
            renderBlockquote(line.substr(pos > 0 ? pos : 2), bqLevel);
            return;
        }
    }

    // Bullet list: - or * with optional indentation
    {
        String trimmed = line;
        int indent = 0;
        while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) {
            indent += (trimmed[0] == '\t') ? 4 : 1;
            trimmed = trimmed.substr(1);
        }
        if (trimmed.starts_with("- ") || trimmed.starts_with("* ")) {
            String prefix(indent, ' ');
            out_ << prefix << AnsiStyle::BRIGHT_YELLOW << "\xe2\x80\xa2" << " " << AnsiStyle::RESET;
            out_ << applyInlineFormatting(trimmed.substr(2)) << "\n";
            return;
        }
    }

    // Numbered list: 1. text
    {
        String trimmed = line;
        int indent = 0;
        while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) {
            indent += (trimmed[0] == '\t') ? 4 : 1;
            trimmed = trimmed.substr(1);
        }
        auto dotPos = trimmed.find(". ");
        if (dotPos != String::npos && dotPos > 0 && dotPos <= 3) {
            bool allDigits = true;
            for (size_t i = 0; i < dotPos; ++i) {
                if (!std::isdigit(trimmed[i])) { allDigits = false; break; }
            }
            if (allDigits) {
                String prefix(indent, ' ');
                out_ << prefix << AnsiStyle::BRIGHT_YELLOW << trimmed.substr(0, dotPos + 2)
                     << AnsiStyle::RESET;
                out_ << applyInlineFormatting(trimmed.substr(dotPos + 2)) << "\n";
                return;
            }
        }
    }

    // Regular text with inline formatting
    out_ << applyInlineFormatting(line) << "\n";
}

void MarkdownRenderer::renderBlockquote(const String& line, int level) {
    for (int i = 0; i < level; ++i) {
        out_ << AnsiStyle::DIM << "\xe2\x96\x8e" << " " << AnsiStyle::RESET;
    }
    if (!line.empty()) {
        out_ << AnsiStyle::DIM << applyInlineFormatting(line) << AnsiStyle::RESET;
    }
    out_ << "\n";
}

void MarkdownRenderer::renderHorizontalRule() {
    // Use box-drawing ─ instead of ASCII -
    String dash = "\xe2\x94\x80";  // ─ (U+2500)
    int width = std::min(terminalWidth_ - 4, 60);
    out_ << AnsiStyle::DIM;
    for (int i = 0; i < width; ++i) out_ << dash;
    out_ << AnsiStyle::RESET << "\n";
}

void MarkdownRenderer::renderTaskList(const String& line) {
    out_ << "  ";
    if (line.starts_with("- [x] ") || line.starts_with("- [X] ")) {
        out_ << AnsiStyle::Semantic::TOOL_SUCCESS << "\xe2\x98\x91" << AnsiStyle::RESET << " ";
        out_ << applyInlineFormatting(line.substr(6)) << "\n";
    } else {
        out_ << AnsiStyle::DIM << "\xe2\x98\x90" << AnsiStyle::RESET << " ";
        out_ << applyInlineFormatting(line.substr(6)) << "\n";
    }
}

// ========== Diff Block ==========

void MarkdownRenderer::renderDiffBlock(const String& code) {
    // Top border
    out_ << AnsiStyle::Semantic::CODE_BORDER
         << "\xe2\x95\xad";
    String dash = "\xe2\x94\x80";
    for (int i = 0; i < terminalWidth_ - 3; ++i) out_ << dash;
    out_ << "\xe2\x95\xae" << AnsiStyle::RESET << "\n";

    std::istringstream stream(code);
    String line;
    while (std::getline(stream, line)) {
        if (line.starts_with("+++ ") || line.starts_with("--- ")) {
            out_ << AnsiStyle::Semantic::DIFF_HEADER << line << AnsiStyle::RESET << "\n";
        } else if (line.starts_with("@@")) {
            out_ << AnsiStyle::Semantic::DIFF_CHUNK << line << AnsiStyle::RESET << "\n";
        } else if (line.starts_with("+")) {
            out_ << AnsiStyle::Semantic::DIFF_ADD << line << AnsiStyle::RESET << "\n";
        } else if (line.starts_with("-")) {
            out_ << AnsiStyle::Semantic::DIFF_REMOVE << line << AnsiStyle::RESET << "\n";
        } else {
            out_ << AnsiStyle::DIM << line << AnsiStyle::RESET << "\n";
        }
    }

    // Bottom border
    out_ << AnsiStyle::Semantic::CODE_BORDER
         << "\xe2\x95\xb0";
    for (int i = 0; i < terminalWidth_ - 3; ++i) out_ << dash;
    out_ << "\xe2\x95\xaf" << AnsiStyle::RESET << "\n";
}

// ========== Table ==========

std::vector<String> MarkdownRenderer::parseTableRow(const String& line) const {
    std::vector<String> cells;
    String row = line;

    // Remove leading/trailing pipes
    if (!row.empty() && row.front() == '|') row = row.substr(1);
    if (!row.empty() && row.back() == '|') row = row.substr(0, row.size() - 1);

    // Split by |
    std::istringstream stream(row);
    String cell;
    while (std::getline(stream, cell, '|')) {
        // Trim whitespace
        size_t start = cell.find_first_not_of(" \t");
        size_t end = cell.find_last_not_of(" \t");
        if (start == String::npos) {
            cells.push_back("");
        } else {
            cells.push_back(cell.substr(start, end - start + 1));
        }
    }
    return cells;
}

bool MarkdownRenderer::isTableSeparator(const String& line) const {
    // Table separator: | --- | --- | or | :---: | ---: |
    String trimmed = line;
    // Remove leading/trailing pipes and whitespace
    if (!trimmed.empty() && trimmed.front() == '|') trimmed = trimmed.substr(1);
    if (!trimmed.empty() && trimmed.back() == '|') trimmed = trimmed.substr(0, trimmed.size() - 1);

    // Split by | and check each cell is all dashes/colons
    std::istringstream stream(trimmed);
    String cell;
    bool hasAtLeastOne = false;
    while (std::getline(stream, cell, '|')) {
        size_t start = cell.find_first_not_of(" \t");
        if (start == String::npos) continue;
        size_t end = cell.find_last_not_of(" \t");
        String content = cell.substr(start, end - start + 1);
        if (content.empty()) continue;
        hasAtLeastOne = true;
        for (char c : content) {
            if (c != '-' && c != ':' && c != ' ') return false;
        }
    }
    return hasAtLeastOne;
}

String MarkdownRenderer::padCell(const String& cell, int width, bool alignRight) {
    String display = cell;
    if (static_cast<int>(display.size()) > width) {
        display = display.substr(0, width - 2) + "..";
    }
    int pad = width - static_cast<int>(display.size());
    if (pad < 0) pad = 0;
    if (alignRight) {
        return String(pad, ' ') + display;
    }
    return display + String(pad, ' ');
}

int MarkdownRenderer::lineNumberWidth(int maxLines) {
    int w = 1;
    while (maxLines >= 10) { maxLines /= 10; w++; }
    return w;
}

void MarkdownRenderer::renderTable(const std::vector<std::vector<String>>& rows, bool hasHeader) {
    if (rows.empty()) return;

    // Compute column widths
    size_t numCols = 0;
    for (const auto& row : rows) {
        numCols = std::max(numCols, row.size());
    }
    if (numCols == 0) return;

    std::vector<int> colWidths(numCols, 0);
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            colWidths[i] = std::max(colWidths[i], static_cast<int>(applyInlineFormatting(row[i]).size()));
        }
    }

    // Add padding (1 space each side) and separators
    int totalWidth = 1; // leading |
    for (size_t i = 0; i < numCols; ++i) {
        colWidths[i] = std::min(colWidths[i], 40); // cap column width
        totalWidth += colWidths[i] + 3; // " cell |"
    }

    // Shrink columns if too wide for terminal
    int maxWidth = terminalWidth_ - 2;
    while (totalWidth > maxWidth && numCols > 0) {
        // Shrink the widest column
        int maxIdx = 0;
        for (size_t i = 1; i < numCols; ++i) {
            if (colWidths[i] > colWidths[maxIdx]) maxIdx = static_cast<int>(i);
        }
        colWidths[maxIdx]--;
        totalWidth--;
    }

    // Render top border
    out_ << AnsiStyle::DIM << "\xe2\x95\xad";  // ╭
    for (size_t i = 0; i < numCols; ++i) {
        String dash = "\xe2\x94\x80";
        for (int j = 0; j < colWidths[i] + 2; ++j) out_ << dash;
        out_ << (i < numCols - 1 ? "\xe2\x94\xac" : "\xe2\x95\xae");  // ┬ / ╮
    }
    out_ << AnsiStyle::RESET << "\n";

    // Render rows
    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];

        // Cell content line
        out_ << AnsiStyle::DIM << "\xe2\x94\x82" << AnsiStyle::RESET;  // │
        for (size_t i = 0; i < numCols; ++i) {
            String cell = (i < row.size()) ? row[i] : "";
            String formatted = applyInlineFormatting(cell);
            if (r == 0 && hasHeader) {
                out_ << " " << AnsiStyle::BOLD << padCell(formatted, colWidths[i]) << AnsiStyle::RESET << " ";
            } else {
                out_ << " " << padCell(formatted, colWidths[i]) << " ";
            }
            out_ << AnsiStyle::DIM << "\xe2\x94\x82" << AnsiStyle::RESET;  // │
        }
        out_ << "\n";

        // Separator after header or between rows
        if (r == 0 && hasHeader) {
            out_ << AnsiStyle::DIM << "\xe2\x94\x9c";  // ├
            for (size_t i = 0; i < numCols; ++i) {
                String dash = "\xe2\x94\x80";
                for (int j = 0; j < colWidths[i] + 2; ++j) out_ << dash;
                out_ << (i < numCols - 1 ? "\xe2\x94\xbc" : "\xe2\x94\xa4");  // ┼ / ┤
            }
            out_ << AnsiStyle::RESET << "\n";
        }
    }

    // Render bottom border
    out_ << AnsiStyle::DIM << "\xe2\x95\xb0";  // ╰
    for (size_t i = 0; i < numCols; ++i) {
        String dash = "\xe2\x94\x80";
        for (int j = 0; j < colWidths[i] + 2; ++j) out_ << dash;
        out_ << (i < numCols - 1 ? "\xe2\x94\xb4" : "\xe2\x95\xaf");  // ┴ / ╯
    }
    out_ << AnsiStyle::RESET << "\n";
}

// ========== Inline Formatting ==========

String MarkdownRenderer::applyInlineFormatting(const String& text) {
    String result = text;

    // Links: [text](url) → text (url)
    result = std::regex_replace(result, std::regex(R"(\[([^\]]+)\]\(([^)]+)\))"),
                                String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET +
                                AnsiStyle::DIM + "($2)" + AnsiStyle::RESET);

    // Inline code: `text`
    result = std::regex_replace(result, std::regex(R"(`([^`]+)`)"),
                                String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);

    // Bold: **text**
    result = std::regex_replace(result, std::regex(R"(\*\*(.+?)\*\*)"),
                                String(AnsiStyle::BOLD) + "$1" + AnsiStyle::RESET);

    // Italic: *text* (avoid matching **)
    result = std::regex_replace(result, std::regex(R"((?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*))"),
                                String(AnsiStyle::ITALIC) + "$1" + AnsiStyle::RESET);

    return result;
}

// ========== Code Block ==========

void MarkdownRenderer::renderCodeBlock(const String& code, const String& lang) {
    // Box-drawing borders: ╭─ lang ───╮ ... ╰────────╯
    String displayLang = lang.empty() ? "code" : lang;

    // Top border: ╭─ lang ────────────╮
    out_ << AnsiStyle::Semantic::CODE_BORDER
         << "\xe2\x95\xad" << "\xe2\x94\x80";  // ╭─
    out_ << " " << AnsiStyle::RESET;
    out_ << AnsiStyle::toolBgColor(lang) << AnsiStyle::toolFgColor(lang)
         << " " << displayLang << " " << AnsiStyle::RESET;
    out_ << " " << AnsiStyle::Semantic::CODE_BORDER;
    int usedWidth = 4 + static_cast<int>(displayLang.length()) + 4;
    int paddingWidth = terminalWidth_ - usedWidth - 2;
    if (paddingWidth < 1) paddingWidth = 1;
    String dash = "\xe2\x94\x80";
    for (int i = 0; i < paddingWidth; ++i) out_ << dash;
    out_ << "\xe2\x95\xae";  // ╮
    out_ << AnsiStyle::RESET << "\n";

    // Code lines with line numbers
    std::istringstream stream(code);
    String line;
    int lineNum = 1;
    int maxLineNum = 0;

    {
        std::istringstream countStream(code);
        String countLine;
        while (std::getline(countStream, countLine)) maxLineNum++;
    }
    int lineNumW = lineNumberWidth(maxLineNum);

    while (std::getline(stream, line)) {
        out_ << AnsiStyle::DIM << std::setw(lineNumW) << lineNum << " "
             << "\xe2\x94\x82" << " " << AnsiStyle::RESET;
        lineNum++;

        int codeWidth = terminalWidth_ - lineNumW - 4;
        String displayLine = line;
        if (static_cast<int>(displayLine.length()) > codeWidth) {
            displayLine = displayLine.substr(0, codeWidth - 3) + "...";
        }

        String highlighted = displayLine;

        // Language-specific keyword highlighting
        if (lang == "cpp" || lang == "c++" || lang == "c" || lang == "hpp") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|else|for|while|return|break|continue|switch|case|default)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(class|struct|void|int|char|bool|float|double|auto|const|static|namespace|using|template|typename)\b)"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(std::\w+|String|Vector|Map|Json)\b)"),
                                           String(AnsiStyle::BRIGHT_GREEN) + "$1" + AnsiStyle::RESET);
        } else if (lang == "python" || lang == "py") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|else|elif|for|while|return|def|class|import|from|as|with|try|except|finally|lambda|yield|async|await)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(True|False|None|self|int|str|list|dict|set|tuple)\b)"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);
        } else if (lang == "rust" || lang == "rs") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|else|for|while|loop|return|break|continue|match|fn|let|mut|const|struct|enum|impl|trait|pub|mod|use|self|Self)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(i32|i64|u32|u64|f32|f64|bool|char|str|String|Vec|Option|Result|Box|Rc|Arc)\b)"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);
        } else if (lang == "go") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|else|for|range|return|break|continue|switch|case|default|func|var|const|type|struct|interface|package|import|go|defer|chan|select)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(int|int64|float64|string|bool|byte|rune|error|nil|true|false)\b)"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);
        } else if (lang == "java" || lang == "kotlin" || lang == "kt") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|else|for|while|return|break|continue|switch|case|default|class|interface|object|fun|val|var|const|public|private|protected|static|final|override|import|package|new|this|super)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(int|long|float|double|boolean|char|String|void|null|true|false)\b)"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);
        } else if (lang == "swift") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|else|for|while|return|break|continue|switch|case|default|func|let|var|class|struct|enum|protocol|extension|public|private|fileprivate|internal|static|override|import|guard|defer|async|await|throws|try|catch)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(Int|Double|Float|Bool|String|Character|Array|Dictionary|Set|Optional|nil|true|false)\b)"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);
        } else if (lang == "javascript" || lang == "js" || lang == "typescript" || lang == "ts") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|else|for|while|return|break|continue|switch|case|default|function|const|let|var|class|interface|type|enum|public|private|protected|static|async|await|import|export|from|new|this|super|try|catch|finally|throw)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(string|number|boolean|void|null|undefined|any|never|true|false|console)\b)"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET);
        } else if (lang == "json") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(([\w\-]+)(\s*:\s*))"),
                                           String(AnsiStyle::BRIGHT_CYAN) + "$1" + AnsiStyle::RESET + "$2");
        } else if (lang == "bash" || lang == "sh" || lang == "shell" || lang == "zsh") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(if|then|else|elif|fi|for|do|done|while|case|esac|function|return|export|source|alias|unset|local|readonly)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
        } else if (lang == "sql") {
            highlighted = std::regex_replace(highlighted, std::regex(R"(\b(SELECT|FROM|WHERE|INSERT|UPDATE|DELETE|CREATE|DROP|ALTER|TABLE|INDEX|JOIN|LEFT|RIGHT|INNER|ON|AND|OR|NOT|NULL|PRIMARY|KEY|FOREIGN|REFERENCES|ORDER|BY|GROUP|HAVING|LIMIT|OFFSET|DISTINCT|UNION)\b)"),
                                           String(AnsiStyle::BRIGHT_MAGENTA) + "$1" + AnsiStyle::RESET);
        }

        // Universal: string literals
        highlighted = std::regex_replace(highlighted, std::regex("\"([^\"]*)\""),
                                       String(AnsiStyle::BRIGHT_GREEN) + "\"$1\"" + AnsiStyle::RESET);
        highlighted = std::regex_replace(highlighted, std::regex("'([^']*)'"),
                                       String(AnsiStyle::BRIGHT_GREEN) + "'$1'" + AnsiStyle::RESET);

        // Universal: comments
        highlighted = std::regex_replace(highlighted, std::regex(R"(//.*$)"),
                                       String(AnsiStyle::DIM) + "$&" + AnsiStyle::RESET);
        highlighted = std::regex_replace(highlighted, std::regex(R"(#.*$)"),
                                       String(AnsiStyle::DIM) + "$&" + AnsiStyle::RESET);
        highlighted = std::regex_replace(highlighted, std::regex(R"(/\*.*\*/)"),
                                       String(AnsiStyle::DIM) + "$&" + AnsiStyle::RESET);

        out_ << highlighted << "\n";
    }

    // Bottom border: ╰──────────────╯
    out_ << AnsiStyle::Semantic::CODE_BORDER
         << "\xe2\x95\xb0";
    for (int i = 0; i < terminalWidth_ - 3; ++i) out_ << dash;
    out_ << "\xe2\x95\xaf"
         << AnsiStyle::RESET << "\n";
}

// ========== Streaming Render ==========

void MarkdownRenderer::renderStream(const String& chunk, StreamState& state) {
    // Append to accumulated text for block-boundary tracking
    state.accumulated += chunk;

    // Prepend any partial line from previous chunk
    String data;
    if (!state.partialLine.empty()) {
        data = state.partialLine + chunk;
        state.partialLine.clear();
    } else {
        data = chunk;
    }

    // Split into lines, keeping last partial line
    std::vector<String> lines;
    size_t start = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '\n') {
            lines.push_back(data.substr(start, i - start));
            start = i + 1;
        }
    }
    // Remaining data (no trailing newline) is a partial line
    if (start < data.size()) {
        state.partialLine = data.substr(start);
    }

    for (const auto& line : lines) {
        if (state.inCodeBlock) {
            if (line.starts_with("```")) {
                state.inCodeBlock = false;
                // Bottom border
                out_ << AnsiStyle::Semantic::CODE_BORDER
                     << "\xe2\x95\xb0";
                String dash = "\xe2\x94\x80";
                for (int i = 0; i < terminalWidth_ - 3; ++i) out_ << dash;
                out_ << "\xe2\x95\xaf"
                     << AnsiStyle::RESET << "\n";
                state.codeLineNum = 0;
            } else {
                // Streaming code line with line number
                state.codeLineNum++;
                int lineNumW = std::max(lineNumberWidth(state.codeMaxLines), 1);
                out_ << AnsiStyle::DIM << std::setw(lineNumW) << state.codeLineNum << " "
                     << "\xe2\x94\x82" << " " << AnsiStyle::RESET << line << "\n";
            }
        } else {
            if (line.starts_with("```")) {
                state.inCodeBlock = true;
                state.codeLang = line.substr(3);
                state.codeLang.erase(0, state.codeLang.find_first_not_of(" \t"));
                state.codeLang.erase(state.codeLang.find_last_not_of(" \t") + 1);
                state.codeLineNum = 0;
                state.codeMaxLines = 0; // unknown during streaming

                // Top border with language badge
                String displayLang = state.codeLang.empty() ? "code" : state.codeLang;
                out_ << AnsiStyle::Semantic::CODE_BORDER
                     << "\xe2\x95\xad" << "\xe2\x94\x80";
                out_ << " " << AnsiStyle::RESET;
                out_ << AnsiStyle::toolBgColor(state.codeLang) << AnsiStyle::toolFgColor(state.codeLang)
                     << " " << displayLang << " " << AnsiStyle::RESET;
                out_ << " " << AnsiStyle::Semantic::CODE_BORDER;
                int usedWidth = 4 + static_cast<int>(displayLang.length()) + 4;
                int paddingWidth = terminalWidth_ - usedWidth - 2;
                if (paddingWidth < 1) paddingWidth = 1;
                String dash = "\xe2\x94\x80";
                for (int i = 0; i < paddingWidth; ++i) out_ << dash;
                out_ << "\xe2\x95\xae";
                out_ << AnsiStyle::RESET << "\n";
            } else {
                renderLine(line);
            }
        }
    }

    // Advance stablePrefixEnd past any confirmed block boundaries.
    // A boundary is confirmed when we see "\n\n" and we're not inside a code block.
    if (!state.inCodeBlock) {
        size_t searchFrom = state.stablePrefixEnd;
        while (searchFrom < state.accumulated.size()) {
            auto pos = state.accumulated.find("\n\n", searchFrom);
            if (pos == String::npos) break;
            // Found a boundary — advance past it
            state.stablePrefixEnd = pos + 2;
            searchFrom = state.stablePrefixEnd;
        }
    }
}

void MarkdownRenderer::finishStream(StreamState& state) {
    state.stablePrefixEnd = 0;
    state.accumulated.clear();
    state.cachedPrefixLines.clear();
}

} // namespace claude
