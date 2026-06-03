#include <claude/ui/FtxuiMarkdown.hpp>
#include <ftxui/screen/string.hpp>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <regex>

namespace claude {

using namespace ftxui;

// Soft macaron colors for markdown rendering
static const auto MdSky      = ftxui::Color::RGB(140, 186, 210);
static const auto MdLavender = ftxui::Color::RGB(180, 160, 210);
static const auto MdSage     = ftxui::Color::RGB(140, 186, 150);
static const auto MdGold     = ftxui::Color::RGB(210, 186, 140);
static const auto MdCream    = ftxui::Color::RGB(200, 195, 180);
static const auto MdShadow   = ftxui::Color::RGB(80, 80, 95);
static const auto MdPeach    = ftxui::Color::RGB(224, 164, 140);
static const auto MdMint     = ftxui::Color::RGB(160, 210, 180);
static const auto MdRose     = ftxui::Color::RGB(210, 150, 150);

// ========== Keyword lists ==========

const std::vector<std::string> FtxuiMarkdown::CPP_KEYWORDS = {
    "auto","break","case","catch","class","const","constexpr","continue","default",
    "delete","do","else","enum","explicit","extern","false","final","for","friend",
    "goto","if","inline","mutable","namespace","new","noexcept","nullptr","operator",
    "override","private","protected","public","register","return","sizeof","static",
    "static_assert","static_cast","struct","switch","template","this","throw","true",
    "try","typedef","typeid","typename","union","using","virtual","volatile","while",
    "#include","#define","#ifdef","#ifndef","#endif","#pragma","std::","void","int",
    "long","double","float","char","bool","unsigned","signed","size_t","string",
    "vector","map","set","optional","expected","unique_ptr","shared_ptr","make_unique",
    "make_shared","return","auto","const","override","noexcept"
};

const std::vector<std::string> FtxuiMarkdown::PYTHON_KEYWORDS = {
    "and","as","assert","async","await","break","class","continue","def","del",
    "elif","else","except","False","finally","for","from","global","if","import",
    "in","is","lambda","None","nonlocal","not","or","pass","raise","return",
    "True","try","while","with","yield","self","print","range","len","list",
    "dict","set","tuple","str","int","float","bool","type","super","__init__"
};

const std::vector<std::string> FtxuiMarkdown::RUST_KEYWORDS = {
    "as","async","await","break","const","continue","crate","dyn","else","enum",
    "extern","false","fn","for","if","impl","in","let","loop","match","mod",
    "move","mut","pub","ref","return","self","Self","static","struct","super",
    "trait","true","type","unsafe","use","where","while","Vec","String","Option",
    "Result","Ok","Err","Some","None","Box","Rc","Arc","println","vec","format"
};

const std::vector<std::string> FtxuiMarkdown::GO_KEYWORDS = {
    "break","case","chan","const","continue","default","defer","else","fallthrough",
    "for","func","go","goto","if","import","interface","map","package","range",
    "return","select","struct","switch","type","var","true","false","nil","iota",
    "append","cap","close","complex","copy","delete","imag","len","make","new",
    "panic","print","println","real","recover","fmt","string","int","bool","error"
};

const std::vector<std::string> FtxuiMarkdown::JS_KEYWORDS = {
    "async","await","break","case","catch","class","const","continue","debugger",
    "default","delete","do","else","export","extends","false","finally","for",
    "function","if","import","in","instanceof","let","new","null","of","return",
    "static","super","switch","this","throw","true","try","typeof","undefined",
    "var","void","while","with","yield","console","require","module","Promise",
    "React","useState","useEffect","useRef","interface","type","enum","implements"
};

const std::vector<std::string> FtxuiMarkdown::BASH_KEYWORDS = {
    "if","then","else","elif","fi","case","esac","for","while","until","do","done",
    "in","function","select","time","coproc","return","exit","break","continue",
    "declare","export","local","readonly","typeset","unset","source","alias","echo",
    "printf","read","cd","pwd","ls","grep","find","awk","sed","sort","uniq","wc",
    "cat","head","tail","mkdir","rm","cp","mv","chmod","chown","sudo","apt","npm",
    "git","docker","curl","wget","set","true","false","test","0"
};

const std::vector<std::string> FtxuiMarkdown::JSON_SPECIAL = {
    "true","false","null"
};

bool FtxuiMarkdown::isKeyword(const std::string& word, const std::string& lang) {
    const std::vector<std::string>* keywords = nullptr;
    if (lang == "cpp" || lang == "c" || lang == "hpp" || lang == "h" || lang == "cc")
        keywords = &CPP_KEYWORDS;
    else if (lang == "python" || lang == "py")
        keywords = &PYTHON_KEYWORDS;
    else if (lang == "rust" || lang == "rs")
        keywords = &RUST_KEYWORDS;
    else if (lang == "go" || lang == "golang")
        keywords = &GO_KEYWORDS;
    else if (lang == "js" || lang == "javascript" || lang == "ts" || lang == "typescript" || lang == "jsx" || lang == "tsx")
        keywords = &JS_KEYWORDS;
    else if (lang == "bash" || lang == "sh" || lang == "zsh" || lang == "shell")
        keywords = &BASH_KEYWORDS;
    else if (lang == "json")
        keywords = &JSON_SPECIAL;
    else
        return false;

    return std::find(keywords->begin(), keywords->end(), word) != keywords->end();
}

// ========== Syntax highlighting ==========

std::string FtxuiMarkdown::highlightLine(const std::string& line, const std::string& lang) {
    if (lang == "json") {
        std::string result;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                size_t end = line.find('"', i + 1);
                if (end != std::string::npos) {
                    std::string str = line.substr(i, end - i + 1);
                    size_t nextNonSpace = line.find_first_not_of(" \t", end + 1);
                    if (nextNonSpace != std::string::npos && line[nextNonSpace] == ':') {
                        result += "\033[36m" + str + "\033[0m";
                    } else if (str == "\"true\"" || str == "\"false\"" || str == "\"null\"") {
                        result += "\033[33m" + str + "\033[0m";
                    } else {
                        result += "\033[32m" + str + "\033[0m";
                    }
                    i = end;
                } else {
                    result += c;
                }
            } else if (c == ':' || c == '{' || c == '}' || c == '[' || c == ']') {
                result += "\033[37m" + std::string(1, c) + "\033[0m";
            } else {
                result += c;
            }
        }
        return result;
    }

    std::string result;
    std::string currentWord;
    bool inString = false;
    char stringChar = 0;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (!inString && (c == '/' && i + 1 < line.size() && line[i+1] == '/')) {
            result += "\033[90m" + line.substr(i) + "\033[0m";
            return result;
        }
        if (!inString && c == '#') {
            if (lang == "bash" || lang == "sh" || lang == "zsh" || lang == "python" || lang == "py") {
                result += "\033[90m" + line.substr(i) + "\033[0m";
                return result;
            }
        }

        if ((c == '"' || c == '\'')) {
            if (!inString) {
                inString = true;
                stringChar = c;
                result += "\033[32m" + std::string(1, c);
            } else if (c == stringChar) {
                result += std::string(1, c) + "\033[0m";
                inString = false;
            } else {
                result += std::string(1, c);
            }
            continue;
        }

        if (inString) {
            result += std::string(1, c);
            continue;
        }

        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@' ||
            (c == '#' && currentWord.empty() && (lang == "cpp" || lang == "c"))) {
            currentWord += c;
        } else {
            if (!currentWord.empty()) {
                if (isKeyword(currentWord, lang)) {
                    result += "\033[35m" + currentWord + "\033[0m";
                } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
                    result += "\033[33m" + currentWord + "\033[0m";
                } else {
                    result += currentWord;
                }
                currentWord.clear();
            }
            result += std::string(1, c);
        }
    }

    if (!currentWord.empty()) {
        if (isKeyword(currentWord, lang)) {
            result += "\033[35m" + currentWord + "\033[0m";
        } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
            result += "\033[33m" + currentWord + "\033[0m";
        } else {
            result += currentWord;
        }
    }

    return result;
}

// ========== Parsing ==========

// Helper: compute display width of a UTF-8 string using FTXUI's native function
static int displayWidth(const std::string& s) {
    return ftxui::string_width(s);
}

// Helper: pad a UTF-8 string to a target display width with spaces
static std::string padToDisplayWidth(const std::string& s, int targetWidth) {
    int currentWidth = displayWidth(s);
    int pad = targetWidth - currentWidth;
    if (pad <= 0) return s;
    return s + std::string(static_cast<size_t>(pad), ' ');
}

// Helper: count leading spaces/tabs for nested list detection
static int countIndent(const std::string& line) {
    int indent = 0;
    for (char c : line) {
        if (c == ' ') indent++;
        else if (c == '\t') indent += 4;
        else break;
    }
    return indent;
}

// Helper: check if a line looks like a table row (starts with |)
static bool isTableRow(const std::string& line) {
    if (line.empty()) return false;
    return line.front() == '|';
}

// Helper: check if a line is a GFM table separator (e.g. | --- | --- |, |------|------|)
static bool isTableSeparator(const std::string& line) {
    if (line.empty() || line.front() != '|') return false;
    // Remove optional trailing |
    std::string content = line;
    if (content.size() > 1 && content.back() == '|') content.pop_back();
    // After removing leading |, remaining should only contain dashes, spaces, colons, and |
    content = content.substr(1);  // Remove leading |
    bool hasDash = false;
    for (char c : content) {
        if (c == '-') hasDash = true;
        else if (c != ' ' && c != ':' && c != '|') return false;
    }
    return hasDash;
}

// Helper: parse a table row like | cell1 | cell2 | cell3 |
static std::vector<std::string> parseTableRow(const std::string& line) {
    std::vector<std::string> cells;

    // Trim leading and trailing |
    std::string remaining = line;
    if (!remaining.empty() && remaining.front() == '|') remaining = remaining.substr(1);
    if (!remaining.empty() && remaining.back() == '|') remaining.pop_back();

    // Split by | — each segment is a cell
    // Use manual splitting to handle edge cases better
    size_t pos = 0;
    while (pos <= remaining.size()) {
        size_t nextPipe = remaining.find('|', pos);
        if (nextPipe == std::string::npos) {
            // Last cell
            std::string cell = remaining.substr(pos);
            // Trim whitespace
            size_t b = cell.find_first_not_of(" \t");
            size_t e = cell.find_last_not_of(" \t");
            if (b == std::string::npos) cells.push_back("");
            else cells.push_back(cell.substr(b, e - b + 1));
            break;
        }
        std::string cell = remaining.substr(pos, nextPipe - pos);
        // Trim whitespace
        size_t b = cell.find_first_not_of(" \t");
        size_t e = cell.find_last_not_of(" \t");
        if (b == std::string::npos) cells.push_back("");
        else cells.push_back(cell.substr(b, e - b + 1));
        pos = nextPipe + 1;
    }
    return cells;
}

// Helper: check if a line is a task list item: - [ ] or - [x]
static bool parseTaskItem(const std::string& line, bool& checked, std::string& text) {
    // Match: - [ ] text or - [x] text (also * or +)
    size_t pos = 0;
    // Skip indent
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    // Skip bullet
    if (pos >= line.size()) return false;
    if (line[pos] != '-' && line[pos] != '*' && line[pos] != '+') return false;
    pos++;
    // Skip space
    if (pos >= line.size() || line[pos] != ' ') return false;
    pos++;
    // Check [ ] or [x]
    if (pos + 2 >= line.size() || line[pos] != '[') return false;
    if (line[pos+1] == ' ') checked = false;
    else if (line[pos+1] == 'x' || line[pos+1] == 'X') checked = true;
    else return false;
    if (line[pos+2] != ']') return false;
    pos += 3;
    // Skip space after ]
    if (pos < line.size() && line[pos] == ' ') pos++;
    text = line.substr(pos);
    return true;
}

std::vector<FtxuiMarkdown::ParsedBlock> FtxuiMarkdown::parse(const std::string& markdown) {
    std::vector<ParsedBlock> blocks;
    std::istringstream stream(markdown);
    std::string line;
    bool inCodeBlock = false;
    CodeBlock currentCode;

    // For table parsing: track header row and separator
    std::vector<std::string> tableHeader;
    std::vector<std::vector<std::string>> tableRows;
    bool inTable = false;

    while (std::getline(stream, line)) {
        if (line.starts_with("```")) {
            if (!inCodeBlock) {
                // Close any open table
                if (inTable) {
                    ParsedBlock block;
                    block.type = ParsedBlock::Table;
                    block.headerCells = std::move(tableHeader);
                    block.rows = std::move(tableRows);
                    blocks.push_back(std::move(block));
                    inTable = false;
                }
                inCodeBlock = true;
                currentCode.lang = line.substr(3);
                auto start = currentCode.lang.find_first_not_of(" \t");
                if (start != std::string::npos) currentCode.lang = currentCode.lang.substr(start);
                else currentCode.lang.clear();
                currentCode.lines.clear();
            } else {
                inCodeBlock = false;
                ParsedBlock block;
                block.type = ParsedBlock::CodeBlockType;
                block.code = std::move(currentCode);
                blocks.push_back(std::move(block));
            }
            continue;
        }

        if (inCodeBlock) {
            currentCode.lines.push_back(line);
            continue;
        }

        // === GFM Table detection ===
        if (isTableRow(line)) {
            if (!inTable) {
                // First table line: could be header or separator
                // If it's a separator, skip it (header will follow or this is malformed)
                if (isTableSeparator(line)) {
                    // Separator before header — unusual but handle gracefully
                    // Don't start a table, treat as potential paragraph
                    // Actually, start table with empty header
                    inTable = true;
                    // Skip the separator — header may come on next line (unlikely)
                    continue;
                }
                // This is the header row
                tableHeader = parseTableRow(line);
                inTable = true;
                continue;
            } else if (isTableSeparator(line)) {
                // Table separator row: skip it
                continue;
            } else {
                // Data row
                tableRows.push_back(parseTableRow(line));
                continue;
            }
        } else if (inTable) {
            // End of table
            ParsedBlock block;
            block.type = ParsedBlock::Table;
            block.headerCells = std::move(tableHeader);
            block.rows = std::move(tableRows);
            blocks.push_back(std::move(block));
            inTable = false;
            // Don't continue — fall through to process this line normally
        }

        if (line == "---" || line == "***" || line == "___") {
            ParsedBlock block;
            block.type = ParsedBlock::HRule;
            blocks.push_back(std::move(block));
            continue;
        }

        if (line.starts_with("###### ")) {
            ParsedBlock block; block.type = ParsedBlock::Header; block.level = 6;
            block.text = line.substr(7); blocks.push_back(std::move(block)); continue;
        }
        if (line.starts_with("##### ")) {
            ParsedBlock block; block.type = ParsedBlock::Header; block.level = 5;
            block.text = line.substr(6); blocks.push_back(std::move(block)); continue;
        }
        if (line.starts_with("#### ")) {
            ParsedBlock block; block.type = ParsedBlock::Header; block.level = 4;
            block.text = line.substr(5); blocks.push_back(std::move(block)); continue;
        }
        if (line.starts_with("### ")) {
            ParsedBlock block; block.type = ParsedBlock::Header; block.level = 3;
            block.text = line.substr(4); blocks.push_back(std::move(block)); continue;
        }
        if (line.starts_with("## ")) {
            ParsedBlock block; block.type = ParsedBlock::Header; block.level = 2;
            block.text = line.substr(3); blocks.push_back(std::move(block)); continue;
        }
        if (line.starts_with("# ")) {
            ParsedBlock block; block.type = ParsedBlock::Header; block.level = 1;
            block.text = line.substr(2); blocks.push_back(std::move(block)); continue;
        }

        // === Task list detection ===
        {
            bool checked;
            std::string taskText;
            if (parseTaskItem(line, checked, taskText)) {
                if (!blocks.empty() && blocks.back().type == ParsedBlock::TaskList) {
                    blocks.back().taskItems.push_back({checked, taskText});
                } else {
                    ParsedBlock block;
                    block.type = ParsedBlock::TaskList;
                    block.taskItems.push_back({checked, taskText});
                    blocks.push_back(std::move(block));
                }
                continue;
            }
        }

        // === Nested list detection ===
        {
            int indent = countIndent(line);
            if (indent > 0 && (line.find("- ") != std::string::npos ||
                               line.find("* ") != std::string::npos ||
                               line.find("+ ") != std::string::npos)) {
                // Indented list item
                size_t bulletPos = line.find_first_of("-*+", indent);
                if (bulletPos != std::string::npos && bulletPos + 2 <= line.size()) {
                    std::string itemText = line.substr(bulletPos + 2);
                    int level = indent / 2;
                    if (!blocks.empty() && blocks.back().type == ParsedBlock::NestedList) {
                        blocks.back().items.push_back(itemText);
                        blocks.back().indentLevel = level;
                    } else {
                        ParsedBlock block;
                        block.type = ParsedBlock::NestedList;
                        block.items.push_back(itemText);
                        block.indentLevel = level;
                        blocks.push_back(std::move(block));
                    }
                    continue;
                }
            }
        }

        if (line.starts_with("- ") || line.starts_with("* ") || line.starts_with("+ ")) {
            if (!blocks.empty() && blocks.back().type == ParsedBlock::BulletList) {
                blocks.back().items.push_back(line.substr(2));
            } else {
                ParsedBlock block;
                block.type = ParsedBlock::BulletList;
                block.items.push_back(line.substr(2));
                blocks.push_back(std::move(block));
            }
            continue;
        }

        {
            size_t dotPos = line.find(". ");
            if (dotPos != std::string::npos && dotPos < 5) {
                bool allDigits = true;
                for (size_t i = 0; i < dotPos; ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(line[i]))) { allDigits = false; break; }
                }
                if (allDigits && dotPos > 0) {
                    std::string item = line.substr(dotPos + 2);
                    if (!blocks.empty() && blocks.back().type == ParsedBlock::NumberedList) {
                        blocks.back().items.push_back(item);
                    } else {
                        ParsedBlock block;
                        block.type = ParsedBlock::NumberedList;
                        block.items.push_back(item);
                        blocks.push_back(std::move(block));
                    }
                    continue;
                }
            }
        }

        if (line.starts_with("> ")) {
            if (!blocks.empty() && blocks.back().type == ParsedBlock::Blockquote) {
                blocks.back().text += "\n" + line.substr(2);
            } else {
                ParsedBlock block;
                block.type = ParsedBlock::Blockquote;
                block.text = line.substr(2);
                blocks.push_back(std::move(block));
            }
            continue;
        }

        if (line.empty()) {
            continue;
        }

        if (!blocks.empty() && blocks.back().type == ParsedBlock::Paragraph) {
            blocks.back().text += "\n" + line;
        } else {
            ParsedBlock block;
            block.type = ParsedBlock::Paragraph;
            block.text = line;
            blocks.push_back(std::move(block));
        }
    }

    // Close any open table
    if (inTable) {
        ParsedBlock block;
        block.type = ParsedBlock::Table;
        block.headerCells = std::move(tableHeader);
        block.rows = std::move(tableRows);
        blocks.push_back(std::move(block));
    }

    if (inCodeBlock) {
        ParsedBlock block;
        block.type = ParsedBlock::CodeBlockType;
        block.code = std::move(currentCode);
        blocks.push_back(std::move(block));
    }

    return blocks;
}

// ========== Inline Rendering ==========

std::vector<Element> FtxuiMarkdown::parseInlineElements(const std::string& src) {
    std::vector<Element> elements;
    std::string current;

    for (size_t i = 0; i < src.size(); ++i) {
        // Strikethrough: ~~text~~
        if (src[i] == '~' && i + 1 < src.size() && src[i+1] == '~') {
            size_t end = src.find("~~", i + 2);
            if (end != std::string::npos) {
                if (!current.empty()) {
                    elements.push_back(ftxui::text(current));
                    current.clear();
                }
                std::string strikeText = src.substr(i + 2, end - i - 2);
                elements.push_back(ftxui::text(strikeText) | dim);
                i = end + 1;
                continue;
            }
        }

        // Inline code: `code`
        if (src[i] == '`' && i + 1 < src.size()) {
            size_t end = src.find('`', i + 1);
            if (end != std::string::npos) {
                if (!current.empty()) {
                    elements.push_back(ftxui::text(current));
                    current.clear();
                }
                std::string code = src.substr(i + 1, end - i - 1);
                elements.push_back(ftxui::text(" " + code + " ") | color(MdSky));
                i = end;
                continue;
            }
        }

        // Bold: **text** or __text__
        if ((src[i] == '*' && i + 1 < src.size() && src[i+1] == '*') ||
            (src[i] == '_' && i + 1 < src.size() && src[i+1] == '_')) {
            char marker = src[i];
            size_t end = src.find(std::string(2, marker), i + 2);
            if (end != std::string::npos) {
                if (!current.empty()) {
                    elements.push_back(ftxui::text(current));
                    current.clear();
                }
                std::string boldText = src.substr(i + 2, end - i - 2);
                elements.push_back(ftxui::text(boldText) | bold);
                i = end + 1;
                continue;
            }
        }

        // Italic: *text* or _text_
        if ((src[i] == '*' || src[i] == '_') && i + 1 < src.size()) {
            char marker = src[i];
            size_t end = src.find(marker, i + 1);
            if (end != std::string::npos && end > i + 1) {
                if (!current.empty()) {
                    elements.push_back(ftxui::text(current));
                    current.clear();
                }
                std::string italicText = src.substr(i + 1, end - i - 1);
                elements.push_back(ftxui::text(italicText) | dim);
                i = end;
                continue;
            }
        }

        // Link: [text](url)
        if (src[i] == '[') {
            size_t textEnd = src.find(']', i + 1);
            if (textEnd != std::string::npos && textEnd + 1 < src.size() && src[textEnd + 1] == '(') {
                size_t urlEnd = src.find(')', textEnd + 2);
                if (urlEnd != std::string::npos) {
                    if (!current.empty()) {
                        elements.push_back(ftxui::text(current));
                        current.clear();
                    }
                    std::string linkText = src.substr(i + 1, textEnd - i - 1);
                    elements.push_back(ftxui::text(linkText) | color(MdSky) | underlined);
                    i = urlEnd;
                    continue;
                }
            }
        }

        current += src[i];
    }

    if (!current.empty()) {
        elements.push_back(ftxui::text(current));
    }

    return elements;
}

Element FtxuiMarkdown::elementsToParagraph(std::vector<Element>&& elems) {
    if (elems.empty()) return ftxui::text("");
    if (elems.size() == 1) return std::move(elems[0]);
    return hbox(std::move(elems));
}

// ========== Block Rendering ==========

Element FtxuiMarkdown::renderCodeBlock(const CodeBlock& code) {
    std::vector<Element> lines;

    if (!code.lang.empty()) {
        lines.push_back(hbox({
            ftxui::text("╭─ " + code.lang + " ─") | color(MdSky) | dim,
            filler(),
            ftxui::text("╮") | color(MdSky) | dim,
        }));
    } else {
        lines.push_back(hbox({
            ftxui::text("╭") | color(MdSky) | dim,
            filler(),
            ftxui::text("╮") | color(MdSky) | dim,
        }));
    }

    int lineNum = 1;
    int totalLines = static_cast<int>(code.lines.size());
    int width = 1;
    while (totalLines >= 10) { width++; totalLines /= 10; }

    for (const auto& rawLine : code.lines) {
        std::string numStr = std::to_string(lineNum);
        while (static_cast<int>(numStr.size()) < width) numStr = " " + numStr;

        auto lineElem = ftxui::text(rawLine.empty() ? std::string(" ") : rawLine);

        lines.push_back(hbox({
            ftxui::text(numStr + " │ ") | dim | color(MdShadow),
            lineElem,
        }));
        lineNum++;
    }

    lines.push_back(hbox({
        ftxui::text("╰") | color(MdSky) | dim,
        filler(),
        ftxui::text("╯") | color(MdSky) | dim,
    }));

    return vbox(std::move(lines));
}

Element FtxuiMarkdown::renderBlock(const ParsedBlock& block) {
    switch (block.type) {
        case ParsedBlock::Paragraph: {
            // Split by newlines — hflow() can't handle embedded \n
            // Each line is rendered separately (paragraph for plain, hflow for inline fmt)
            std::vector<Element> lineElems;
            std::istringstream pstream(block.text);
            std::string pLine;
            while (std::getline(pstream, pLine)) {
                if (pLine.find('`') != std::string::npos ||
                    pLine.find("**") != std::string::npos ||
                    pLine.find("~~") != std::string::npos ||
                    pLine.find('[') != std::string::npos) {
                    auto inlineElems = parseInlineElements(pLine);
                    lineElems.push_back(hflow(std::move(inlineElems)));
                } else {
                    lineElems.push_back(ftxui::paragraph(pLine));
                }
            }
            if (lineElems.empty()) return ftxui::text("");
            if (lineElems.size() == 1) return std::move(lineElems[0]);
            return vbox(std::move(lineElems));
        }

        case ParsedBlock::CodeBlockType:
            return renderCodeBlock(block.code);

        case ParsedBlock::Header: {
            Color headerColor;
            switch (block.level) {
                case 1: headerColor = MdLavender; break;
                case 2: headerColor = MdSage; break;
                case 3: headerColor = MdSky; break;
                default: headerColor = MdCream; break;
            }
            auto inlineElems = parseInlineElements(block.text);
            Element content = elementsToParagraph(std::move(inlineElems));
            return hbox({
                ftxui::text(std::string(block.level, '#') + " ") | dim | color(headerColor),
                content | bold | color(headerColor),
            });
        }

        case ParsedBlock::BulletList: {
            std::vector<Element> items;
            for (const auto& item : block.items) {
                // Use paragraph with bullet prefix so text wraps naturally
                items.push_back(ftxui::paragraph("  • " + item) | color(MdGold));
            }
            return vbox(std::move(items));
        }

        case ParsedBlock::NumberedList: {
            std::vector<Element> items;
            int num = 1;
            for (const auto& item : block.items) {
                items.push_back(ftxui::paragraph("  " + std::to_string(num) + ". " + item) | color(MdGold));
                num++;
            }
            return vbox(std::move(items));
        }

        case ParsedBlock::Blockquote: {
            return ftxui::paragraph("▎ " + block.text) | dim | color(MdSky);
        }

        case ParsedBlock::HRule:
            return hbox({
                ftxui::text("─") | flex | dim | color(MdShadow),
            });

        case ParsedBlock::Table: {
            auto numCols = std::max(block.headerCells.size(), size_t(1));

            // Compute per-column max content width
            std::vector<int> colWidths(numCols, 3);
            for (size_t c = 0; c < numCols; ++c) {
                if (c < block.headerCells.size())
                    colWidths[c] = std::max(colWidths[c], displayWidth(block.headerCells[c]));
                for (const auto& row : block.rows) {
                    if (c < row.size())
                        colWidths[c] = std::max(colWidths[c], displayWidth(row[c]));
                }
            }

            // Build each row as a single preformatted string — no hbox, no flex compression
            std::vector<Element> rows;

            // Helper: build a horizontal border line with box-drawing chars
            auto makeHBorder = [&](const char* left, const char* mid, const char* right, const std::string& fill) {
                std::string line = left;
                for (size_t c = 0; c < numCols; ++c) {
                    for (int d = 0; d < colWidths[c] + 2; ++d) line += fill;
                    if (c + 1 < numCols) line += mid;
                }
                line += right;
                return line;
            };

            // Top border
            rows.push_back(ftxui::text(makeHBorder("╭", "┬", "╮", "─")) | color(MdSky) | dim);

            // Header row — color the border chars to match horizontal borders
            if (!block.headerCells.empty()) {
                std::vector<ftxui::Element> headerParts;
                for (size_t c = 0; c < numCols; ++c) {
                    if (c == 0) headerParts.push_back(ftxui::text("│") | color(MdSky) | dim);
                    std::string cell = c < block.headerCells.size() ? block.headerCells[c] : "";
                    headerParts.push_back(ftxui::text(" " + padToDisplayWidth(cell, colWidths[c]) + " ") | bold);
                    headerParts.push_back(ftxui::text("│") | color(MdSky) | dim);
                }
                rows.push_back(ftxui::hbox(std::move(headerParts)));
            }

            // Header/data separator
            rows.push_back(ftxui::text(makeHBorder("├", "┼", "┤", "─")) | color(MdSky) | dim);

            // Data rows — color the border chars to match horizontal borders
            for (const auto& row : block.rows) {
                std::vector<ftxui::Element> rowParts;
                for (size_t c = 0; c < numCols; ++c) {
                    if (c == 0) rowParts.push_back(ftxui::text("│") | color(MdSky) | dim);
                    std::string cell = c < row.size() ? row[c] : "";
                    rowParts.push_back(ftxui::text(" " + padToDisplayWidth(cell, colWidths[c]) + " "));
                    rowParts.push_back(ftxui::text("│") | color(MdSky) | dim);
                }
                rows.push_back(ftxui::hbox(std::move(rowParts)));
            }

            // Bottom border
            rows.push_back(ftxui::text(makeHBorder("╰", "┴", "╯", "─")) | color(MdSky) | dim);

            auto tableElem = vbox(std::move(rows));
            // Ensure table doesn't overflow terminal width — flex shrinks if needed
            return tableElem | ftxui::xflex_grow | ftxui::xflex_shrink;
        }

        case ParsedBlock::TaskList: {
            std::vector<Element> items;
            for (const auto& [checked, text] : block.taskItems) {
                std::string prefix = checked ? "  ☑ " : "  ☐ ";
                items.push_back(ftxui::paragraph(prefix + text)
                    | color(checked ? MdMint : MdCream)
                    | (checked ? dim : nothing));
            }
            return vbox(std::move(items));
        }

        case ParsedBlock::NestedList: {
            std::vector<Element> items;
            for (const auto& item : block.items) {
                int indent = block.indentLevel;
                std::string prefix = std::string(indent * 2, ' ') + "  • ";
                items.push_back(ftxui::paragraph(prefix + item) | color(MdGold));
            }
            return vbox(std::move(items));
        }
    }
    return ftxui::text("");
}

// ========== Public API ==========

bool FtxuiMarkdown::hasMarkdownSyntax(const std::string& text) {
    // Fast regex check for common Markdown markers
    static const std::regex mdSyntax(
        R"(([#`~>*|\[\]!-]|\d+\.\s|\*\*|~~))",
        std::regex::optimize
    );
    return std::regex_search(text, mdSyntax);
}

std::vector<Element> FtxuiMarkdown::render(const std::string& markdown) {
    // Fast path: skip the parser for plain text with no Markdown syntax
    if (!hasMarkdownSyntax(markdown)) {
        return {ftxui::paragraph(markdown)};
    }

    auto blocks = parse(markdown);
    std::vector<Element> result;
    result.reserve(blocks.size());

    for (const auto& block : blocks) {
        result.push_back(renderBlock(block));
    }

    return result;
}

Element FtxuiMarkdown::renderInline(const std::string& src) {
    auto inlineElems = parseInlineElements(src);
    return elementsToParagraph(std::move(inlineElems));
}

// ========== Streaming Renderer ==========

void FtxuiMarkdown::StreamingRenderer::append(const std::string& chunk) {
    fullText_ += chunk;

    // Find the new stable boundary
    size_t newBoundary = findStableBoundary();

    // Commit any newly completed blocks to the cache
    if (newBoundary > stablePrefixEnd_) {
        commitStableBlocks(newBoundary);
    }

    // Update the unstable suffix
    unstableSuffix_ = fullText_.substr(stablePrefixEnd_);
}

std::vector<ftxui::Element> FtxuiMarkdown::StreamingRenderer::render() {
    std::vector<ftxui::Element> result;

    // Copy cached elements from the stable prefix
    result.insert(result.end(), completedElements_.begin(), completedElements_.end());

    // Parse and render only the unstable suffix
    if (!unstableSuffix_.empty()) {
        auto blocks = parse(unstableSuffix_);
        for (const auto& block : blocks) {
            result.push_back(renderBlock(block));
        }
    }

    return result;
}

std::vector<ftxui::Element> FtxuiMarkdown::StreamingRenderer::finalize() {
    // Commit everything remaining as stable
    if (stablePrefixEnd_ < fullText_.size()) {
        commitStableBlocks(fullText_.size());
        unstableSuffix_.clear();
    }

    auto result = std::move(completedElements_);
    completedElements_.clear();
    return result;
}

void FtxuiMarkdown::StreamingRenderer::reset() {
    fullText_.clear();
    completedElements_.clear();
    stablePrefixEnd_ = 0;
    unstableSuffix_.clear();
}

size_t FtxuiMarkdown::StreamingRenderer::findStableBoundary() const {
    if (fullText_.empty()) return 0;

    bool inCodeBlock = false;
    size_t lastSafeBreak = 0;

    // Scan from stablePrefixEnd_ forward. Only commit at blank-line boundaries
    // where the next line does NOT start with '|' (i.e., not entering a table).
    // Never commit at end-of-text — incomplete tables must stay unstable
    // until finalize() commits everything, or a blank line terminates the table.
    for (size_t i = stablePrefixEnd_; i < fullText_.size(); ++i) {
        if (fullText_[i] == '`' && i + 2 < fullText_.size() &&
            fullText_[i+1] == '`' && fullText_[i+2] == '`') {
            inCodeBlock = !inCodeBlock;
            i += 2;
            continue;
        }

        if (inCodeBlock) continue;

        if (fullText_[i] == '\n') {
            size_t nextNonNewline = fullText_.find_first_not_of("\n", i);
            if (nextNonNewline != std::string::npos && nextNonNewline > i + 1) {
                // Blank line found — candidate break point
                if (nextNonNewline < fullText_.size() && fullText_[nextNonNewline] == '|') {
                    // Table starts right after this blank line — don't break here
                    continue;
                }
                lastSafeBreak = nextNonNewline;
            } else if (nextNonNewline == std::string::npos) {
                // Blank line at end of text (trailing newlines) — safe to commit
                // because no table content can follow
                lastSafeBreak = fullText_.size();
            }
            // NOTE: single \n at end of text (e.g. "| data |\n") does NOT
            // produce a safe break — table lines must stay unstable until
            // a blank line terminates the table or finalize() is called.
        }
    }

    return lastSafeBreak > stablePrefixEnd_ ? lastSafeBreak : stablePrefixEnd_;
}

void FtxuiMarkdown::StreamingRenderer::commitStableBlocks(size_t newBoundary) {
    // Parse the newly stable text (from stablePrefixEnd_ to newBoundary)
    std::string newlyStable = fullText_.substr(stablePrefixEnd_, newBoundary - stablePrefixEnd_);

    auto blocks = parse(newlyStable);
    for (const auto& block : blocks) {
        completedElements_.push_back(renderBlock(block));
    }

    stablePrefixEnd_ = newBoundary;
}

} // namespace claude
