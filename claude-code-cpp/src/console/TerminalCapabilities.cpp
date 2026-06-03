#include <claude/console/TerminalCapabilities.hpp>
#include <cstdlib>
#include <string>

namespace claude::console {

int TerminalCapabilities::detectColorLevel() {
    // Check COLORTERM first — the most reliable indicator of truecolor support
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm) {
        std::string ct(colorterm);
        if (ct.find("truecolor") != std::string::npos ||
            ct.find("24bit") != std::string::npos) {
            return 3;
        }
    }

    // Fall back to TERM environment variable
    const char* term = std::getenv("TERM");
    if (term) {
        std::string t(term);
        if (t.find("256color") != std::string::npos) return 2;
        if (t.find("xterm") != std::string::npos) return 1;
    }

    // Default: assume at least 16-color support
    return 1;
}

} // namespace claude::console
