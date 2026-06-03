#pragma once

namespace claude::console {

/// Detects the terminal's color support level for auto-downgrading themes.
/// Level 0 = mono, 1 = 16-color, 2 = 256-color, 3 = truecolor (24-bit).
class TerminalCapabilities {
public:
    /// Returns color level: 0=mono, 1=16-color, 2=256-color, 3=truecolor (24-bit)
    static int detectColorLevel();

    static bool supportsTrueColor() { return detectColorLevel() >= 3; }
    static bool supports256Color() { return detectColorLevel() >= 2; }
    static bool supportsAtLeast16Color() { return detectColorLevel() >= 1; }
};

} // namespace claude::console
