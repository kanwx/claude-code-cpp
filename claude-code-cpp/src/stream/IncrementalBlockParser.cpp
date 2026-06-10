#include "claude/stream/IncrementalBlockParser.hpp"
#include <regex>

namespace claude {

bool IncrementalBlockParser::append(const String& text) {
    lineBuffer_ += text;
    bool foundBoundary = false;

    size_t newlinePos;
    while ((newlinePos = lineBuffer_.find('\n')) != String::npos) {
        String line = lineBuffer_.substr(0, newlinePos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lineBuffer_ = lineBuffer_.substr(newlinePos + 1);

        if (processLine(line)) {
            foundBoundary = true;
        }
        boundaryPos_ += line.size() + 1;
    }
    return foundBoundary;
}

size_t IncrementalBlockParser::lastBoundaryPos() const {
    return boundaryPos_;
}

void IncrementalBlockParser::reset() {
    context_ = LineContext::Normal;
    fenceLang_.clear();
    boundaryPos_ = 0;
    lineBuffer_.clear();
}

bool IncrementalBlockParser::processLine(const String& line) {
    if (context_ == LineContext::InCodeFence) {
        if (isCodeFenceClose(line)) {
            context_ = LineContext::Normal;
            fenceLang_.clear();
            return true;
        }
        return false;
    }

    if (line.empty() || std::all_of(line.begin(), line.end(), [](char c) { return c == ' ' || c == '\t'; })) {
        if (context_ == LineContext::Blank) {
            return false;
        }
        context_ = LineContext::Blank;
        return true;
    }

    if (context_ == LineContext::Blank) {
        if (isAtXHeader(line))       { context_ = LineContext::Normal; return true; }
        if (isCodeFenceOpen(line))   { context_ = LineContext::InCodeFence; return true; }
        if (isListItemStart(line))   { context_ = LineContext::Normal; return true; }
        if (isBlockquoteStart(line)) { context_ = LineContext::Normal; return true; }
        if (isHorizontalRule(line))  { context_ = LineContext::Normal; return true; }
    }

    if (context_ == LineContext::Normal && isCodeFenceOpen(line)) {
        context_ = LineContext::InCodeFence;
        return true;
    }

    context_ = LineContext::Normal;
    return false;
}

bool IncrementalBlockParser::isAtXHeader(const String& line) const {
    static std::regex re("^#{1,6}\\s");
    return std::regex_search(line, re);
}

bool IncrementalBlockParser::isCodeFenceOpen(const String& line) const {
    if (line.size() < 3) return false;
    char c = line[0];
    if (c != '`' && c != '~') return false;
    if (line[0] != line[1] || line[1] != line[2]) return false;
    return true;
}

bool IncrementalBlockParser::isCodeFenceClose(const String& line) const {
    if (line.size() < 3) return false;
    char c = line[0];
    if (c != '`' && c != '~') return false;
    if (line[0] != line[1] || line[1] != line[2]) return false;
    for (size_t i = 3; i < line.size(); ++i) {
        if (line[i] != c && line[i] != ' ' && line[i] != '\t') return false;
    }
    return true;
}

bool IncrementalBlockParser::isListItemStart(const String& line) const {
    static std::regex re("^[\\s]*[-*+]\\s|^[\\s]*\\d+\\.\\s");
    return std::regex_search(line, re);
}

bool IncrementalBlockParser::isBlockquoteStart(const String& line) const {
    return !line.empty() && line[0] == '>';
}

bool IncrementalBlockParser::isHorizontalRule(const String& line) const {
    static std::regex re("^\\s*([-*_])\\s*(\\1\\s*){2,}$");
    return std::regex_search(line, re);
}

} // namespace claude
