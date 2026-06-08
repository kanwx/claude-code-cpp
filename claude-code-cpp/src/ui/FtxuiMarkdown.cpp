#include <claude/ui/FtxuiMarkdown.hpp>
#include <claude/ui/LanguageSyntax.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <ftxui/screen/string.hpp>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cstdint>
#include <list>
#include <optional>
#include <regex>
#include <unordered_map>
#include <cstdlib>

namespace claude {

using namespace ftxui;

// ========== Markdown LRU Cache ==========

namespace {
class MarkdownCache {
public:
    using CacheKey = uint64_t;

    std::optional<std::vector<Element>> get(CacheKey key) {
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            lruList_.splice(lruList_.begin(), lruList_, it->second.second);
            return it->second.first;
        }
        return std::nullopt;
    }

    void put(CacheKey key, std::vector<Element> elements) {
        if (cache_.size() >= maxSize_) {
            evictOldest();
        }
        lruList_.push_front(key);
        cache_[key] = {std::move(elements), lruList_.begin()};
    }

    void clear() {
        cache_.clear();
        lruList_.clear();
    }

private:
    void evictOldest() {
        auto key = lruList_.back();
        lruList_.pop_back();
        cache_.erase(key);
    }

    static constexpr size_t maxSize_ = 200;
    std::unordered_map<CacheKey,
        std::pair<std::vector<Element>, std::list<CacheKey>::iterator>> cache_;
    std::list<CacheKey> lruList_;
};

// Simple hash function (FNV-1a)
uint64_t contentHash(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : s) {
        h ^= static_cast<uint64_t>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

MarkdownCache g_markdownCache;
} // namespace

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

// ========== Syntax highlighting ==========
// Keyword tables and language definitions are in LanguageSyntaxRegistry (LanguageSyntax.hpp).
// highlightLine() uses a state-machine that respects string/comment delimiters per language.

namespace {

/// ANSI escape codes used by the FTXUI highlighter (applied inside ftxui::text strings)
static constexpr const char* HL_KEYWORD    = "\033[35m";  // magenta – keywords
static constexpr const char* HL_TYPE       = "\033[36m";  // cyan    – type-like keywords
static constexpr const char* HL_STRING     = "\033[32m";  // green   – string literals
static constexpr const char* HL_NUMBER     = "\033[33m";  // yellow  – numeric literals
static constexpr const char* HL_COMMENT    = "\033[90m";  // bright-black (dim gray) – comments
static constexpr const char* HL_PUNCT      = "\033[37m";  // white   – punctuation
static constexpr const char* HL_RESET      = "\033[0m";

/// Try to match any of the candidate strings at position `pos` in `line`.
/// Returns the length of the match (0 if none matched).
size_t matchAnyAt(const std::string& line, size_t pos,
                  const std::vector<std::pair<std::string,std::string>>& candidates) {
    for (const auto& [open, _close] : candidates) {
        if (pos + open.size() <= line.size() &&
            line.compare(pos, open.size(), open) == 0) {
            return open.size();
        }
    }
    return 0;
}

/// Find the closing delimiter starting from `pos` (after the opening delimiter).
/// Returns the index just past the closing delimiter, or std::string::npos.
size_t findClosing(const std::string& line, size_t pos,
                   const std::string& open, const std::string& close) {
    // For symmetric delimiters (open==close), find next occurrence after pos.
    if (open == close) {
        size_t next = line.find(close, pos);
        return (next == std::string::npos) ? std::string::npos : next + close.size();
    }
    // For asymmetric delimiters, just find the close.
    size_t next = line.find(close, pos);
    return (next == std::string::npos) ? std::string::npos : next + close.size();
}

/// Convert an ANSI-embedded string (from highlightLine) into a single ftxui Element.
/// Parses the SGR escape sequences (\033[XXm) used by the highlighter and builds
/// an hbox of text segments, each with the appropriate ftxui::color() decorator.
Element ansiToFtxuiElement(const std::string& ansiStr) {
    // Mapping from ANSI SGR codes to ftxui Colors (mirrors HL_* constants above)
    static const auto sgrToColor = [](int code) -> ftxui::Color {
        switch (code) {
            case 35: return ftxui::Color::RGB(180, 80, 180);   // magenta  (keyword)
            case 36: return ftxui::Color::RGB(80, 180, 180);   // cyan     (type)
            case 32: return ftxui::Color::RGB(80, 180, 80);    // green    (string)
            case 33: return ftxui::Color::RGB(210, 186, 140);  // yellow   (number) — matches MdGold
            case 90: return ftxui::Color::RGB(120, 120, 120);  // bright-black (comment)
            case 37: return ftxui::Color::RGB(180, 180, 180);  // white    (punctuation)
            case 0:  return ftxui::Color{};                     // reset = default
            default: return ftxui::Color{};
        }
    };

    std::vector<Element> segments;
    std::string currentText;
    ftxui::Color currentColor;   // default-constructed = "no override"
    bool hasColor = false;

    auto flush = [&]() {
        if (!currentText.empty()) {
            Element e = ftxui::text(std::move(currentText));
            currentText.clear();
            if (hasColor) {
                e = std::move(e) | color(currentColor);
            }
            segments.push_back(std::move(e));
        }
    };

    size_t i = 0;
    while (i < ansiStr.size()) {
        // Look for ESC[ ... m
        if (ansiStr[i] == '\033' && i + 1 < ansiStr.size() && ansiStr[i+1] == '[') {
            size_t mPos = ansiStr.find('m', i + 2);
            if (mPos != std::string::npos) {
                flush();
                // Parse the SGR code(s) — take the last numeric value
                std::string codeStr = ansiStr.substr(i + 2, mPos - i - 2);
                int code = 0;
                // Handle compound codes like "0;35" — use the last number
                size_t lastSemi = codeStr.rfind(';');
                if (lastSemi != std::string::npos) {
                    code = std::atoi(codeStr.c_str() + lastSemi + 1);
                } else if (!codeStr.empty()) {
                    code = std::atoi(codeStr.c_str());
                }
                if (code == 0) {
                    hasColor = false;
                } else {
                    currentColor = sgrToColor(code);
                    hasColor = true;
                }
                i = mPos + 1;
                continue;
            }
        }
        currentText += ansiStr[i];
        ++i;
    }
    flush();

    if (segments.empty()) return ftxui::text("");
    if (segments.size() == 1) return std::move(segments[0]);
    return hbox(std::move(segments));
}

} // anonymous namespace

std::string FtxuiMarkdown::highlightLine(const std::string& line, const std::string& lang) {
    // Env guard: CLAUDE_CODE_SYNTAX_HIGHLIGHT=0 disables highlighting
    if (LanguageSyntaxRegistry::isDisabled()) return line;

    const LanguageSyntax* syn = LanguageSyntaxRegistry::instance().getOrAlias(lang);

    // ── JSON special path (key vs value string distinction) ──
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
                        result += HL_TYPE + str + HL_RESET;
                    } else if (str == "\"true\"" || str == "\"false\"" || str == "\"null\"") {
                        result += HL_NUMBER + str + HL_RESET;
                    } else {
                        result += HL_STRING + str + HL_RESET;
                    }
                    i = end;
                } else {
                    result += c;
                }
            } else if (c == ':' || c == '{' || c == '}' || c == '[' || c == ']') {
                result += HL_PUNCT + std::string(1, c) + HL_RESET;
            } else {
                result += c;
            }
        }
        return result;
    }

    // If no syntax definition, return plain text (preserving old behavior for unknown langs)
    if (!syn) return line;

    // ── State-machine tokenizer ──
    std::string result;
    std::string currentWord;
    size_t i = 0;

    while (i < line.size()) {
        // 1. Try block comment open
        if (!syn->blockCommentDelimiters.empty()) {
            size_t matchLen = matchAnyAt(line, i, syn->blockCommentDelimiters);
            if (matchLen > 0) {
                // Find which delimiter matched
                const std::string* openDelim = nullptr;
                const std::string* closeDelim = nullptr;
                for (const auto& [o, c] : syn->blockCommentDelimiters) {
                    if (line.compare(i, o.size(), o) == 0) {
                        openDelim = &o;
                        closeDelim = &c;
                        break;
                    }
                }
                if (openDelim && closeDelim) {
                    // Flush current word
                    if (!currentWord.empty()) {
                        if (syn->isKeyword(currentWord)) {
                            result += HL_KEYWORD + currentWord + HL_RESET;
                        } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
                            result += HL_NUMBER + currentWord + HL_RESET;
                        } else {
                            result += currentWord;
                        }
                        currentWord.clear();
                    }
                    // Find closing on this line
                    size_t closePos = findClosing(line, i + matchLen, *openDelim, *closeDelim);
                    if (closePos != std::string::npos) {
                        result += HL_COMMENT + line.substr(i, closePos - i) + HL_RESET;
                        i = closePos;
                        continue;
                    } else {
                        // Block comment extends past this line – color rest as comment
                        result += HL_COMMENT + line.substr(i) + HL_RESET;
                        return result;
                    }
                }
            }
        }

        // 2. Try string delimiter open
        if (!syn->stringDelimiters.empty()) {
            size_t matchLen = matchAnyAt(line, i, syn->stringDelimiters);
            if (matchLen > 0) {
                // Find which delimiter matched
                const std::string* openDelim = nullptr;
                const std::string* closeDelim = nullptr;
                for (const auto& [o, c] : syn->stringDelimiters) {
                    if (line.compare(i, o.size(), o) == 0) {
                        openDelim = &o;
                        closeDelim = &c;
                        break;
                    }
                }
                if (openDelim && closeDelim) {
                    // Flush current word
                    if (!currentWord.empty()) {
                        if (syn->isKeyword(currentWord)) {
                            result += HL_KEYWORD + currentWord + HL_RESET;
                        } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
                            result += HL_NUMBER + currentWord + HL_RESET;
                        } else {
                            result += currentWord;
                        }
                        currentWord.clear();
                    }
                    // Find closing
                    size_t closePos = findClosing(line, i + matchLen, *openDelim, *closeDelim);
                    if (closePos != std::string::npos) {
                        result += HL_STRING + line.substr(i, closePos - i) + HL_RESET;
                        i = closePos;
                        continue;
                    } else {
                        // String extends past this line – color rest as string
                        result += HL_STRING + line.substr(i) + HL_RESET;
                        return result;
                    }
                }
            }
        }

        // 3. Try line comment
        if (syn->hasLineComments && !syn->lineCommentStart.empty()) {
            if (i + syn->lineCommentStart.size() <= line.size() &&
                line.compare(i, syn->lineCommentStart.size(), syn->lineCommentStart) == 0) {
                // Flush current word
                if (!currentWord.empty()) {
                    if (syn->isKeyword(currentWord)) {
                        result += HL_KEYWORD + currentWord + HL_RESET;
                    } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
                        result += HL_NUMBER + currentWord + HL_RESET;
                    } else {
                        result += currentWord;
                    }
                    currentWord.clear();
                }
                // Color rest of line as comment
                result += HL_COMMENT + line.substr(i) + HL_RESET;
                return result;
            }
        }

        // 4. PHP also supports # line comments
        if (lang == "php" && line[i] == '#') {
            if (!currentWord.empty()) {
                if (syn->isKeyword(currentWord)) {
                    result += HL_KEYWORD + currentWord + HL_RESET;
                } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
                    result += HL_NUMBER + currentWord + HL_RESET;
                } else {
                    result += currentWord;
                }
                currentWord.clear();
            }
            result += HL_COMMENT + line.substr(i) + HL_RESET;
            return result;
        }

        // 5. Build word (alphanumeric, underscore, @, #prefix for C preprocessor)
        char c = line[i];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@' ||
            (c == '#' && currentWord.empty() && (lang == "cpp" || lang == "c" || lang == "hpp" || lang == "h" || lang == "cc"))) {
            currentWord += c;
        } else {
            // Flush current word
            if (!currentWord.empty()) {
                if (syn->isKeyword(currentWord)) {
                    result += HL_KEYWORD + currentWord + HL_RESET;
                } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
                    result += HL_NUMBER + currentWord + HL_RESET;
                } else {
                    result += currentWord;
                }
                currentWord.clear();
            }
            result += std::string(1, c);
        }
        ++i;
    }

    // Flush trailing word
    if (!currentWord.empty()) {
        if (syn->isKeyword(currentWord)) {
            result += HL_KEYWORD + currentWord + HL_RESET;
        } else if (std::isdigit(static_cast<unsigned char>(currentWord[0]))) {
            result += HL_NUMBER + currentWord + HL_RESET;
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

        // Link: [text](url) — OSC 8 hyperlinks for URLs, plain text for mailto:
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
                    std::string url = src.substr(textEnd + 2, urlEnd - textEnd - 2);

                    if (url.size() >= 7 && url.substr(0, 7) == "mailto:") {
                        // mailto: links — display email as plain text (no hyperlink wrapping)
                        elements.push_back(ftxui::text(linkText) | color(MdSky));
                    } else {
                        // Regular links — wrap with OSC 8 hyperlink sequence
                        // FTXUI's terminal backend passes through ANSI codes, so the hyperlink works
                        std::string hyperlinkText = AnsiStyle::createHyperlink(url, linkText);
                        elements.push_back(ftxui::text(hyperlinkText) | color(MdSky));
                    }
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

        Element lineElem;
        if (!code.lang.empty() && !rawLine.empty()) {
            // Apply syntax highlighting: highlightLine returns ANSI-embedded text,
            // ansiToFtxuiElement converts it to properly colored ftxui Elements.
            lineElem = ansiToFtxuiElement(highlightLine(rawLine, code.lang));
        } else {
            lineElem = ftxui::text(rawLine.empty() ? std::string(" ") : rawLine);
        }

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

    // Check cache — static messages re-entering the viewport after OffscreenFreeze
    // don't need re-parsing; cache the fully rendered Elements for instant reuse.
    auto key = contentHash(markdown);
    if (auto cached = g_markdownCache.get(key)) {
        return *cached;
    }

    // Parse and render
    auto blocks = parse(markdown);
    std::vector<Element> result;
    result.reserve(blocks.size());

    for (const auto& block : blocks) {
        result.push_back(renderBlock(block));
    }

    // Store in cache for future hits
    g_markdownCache.put(key, result);

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
