#pragma once

#include "claude/core/Types.hpp"

namespace claude {

class IncrementalBlockParser {
public:
    bool append(const String& text);
    size_t lastBoundaryPos() const;
    void reset();

private:
    enum class LineContext {
        Normal,
        Blank,
        InCodeFence,
    };
    LineContext context_ = LineContext::Normal;
    String fenceLang_;
    size_t boundaryPos_ = 0;
    String lineBuffer_;

    bool processLine(const String& line);
    bool isAtXHeader(const String& line) const;
    bool isCodeFenceOpen(const String& line) const;
    bool isCodeFenceClose(const String& line) const;
    bool isListItemStart(const String& line) const;
    bool isBlockquoteStart(const String& line) const;
    bool isHorizontalRule(const String& line) const;
};

} // namespace claude
