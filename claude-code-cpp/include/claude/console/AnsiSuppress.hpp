#pragma once

#include <cstdlib>
#include <string>
#include <unistd.h>

namespace claude {

/// Whether ANSI escape codes may be written to stdout.
/// Returns false when stdout is not a TTY (pipe/redirect) or NO_COLOR is set.
inline bool supportsAnsiStdout() {
    if (!isatty(STDOUT_FILENO)) return false;
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return true;
}

/// Whether ANSI escape codes may be written to stderr.
/// Returns false when stderr is not a TTY or NO_COLOR is set.
inline bool supportsAnsiStderr() {
    if (!isatty(STDERR_FILENO)) return false;
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return true;
}

/// Strip ANSI SGR / cursor-save-restore escape sequences from a string.
/// Kept minimal: only removes CSI sequences (ESC [ ... m) and cursor
/// save/restore (ESC [ s / ESC [ u). Does not handle OSC sequences
/// (we don't emit any to stdout currently).
inline std::string stripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033') {
            if (i + 1 < s.size() && s[i + 1] == '[') {
                // skip until we find a final byte in the range @–~
                i += 2;  // skip ESC [
                while (i < s.size() && (s[i] < '@' || s[i] > '~')) ++i;
                // i now points to the final byte; loop increment will advance past it
            } else if (i + 1 < s.size() && s[i + 1] == ']') {
                // OSC sequence — skip until ST (ESC \) or BEL
                i += 2;  // skip ESC ]
                while (i < s.size() && s[i] != '\033' && s[i] != '\a') ++i;
                if (i < s.size() && s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '\\') ++i;
            }
            // else: lone ESC, skip it
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace claude
