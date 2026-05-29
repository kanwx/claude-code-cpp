#pragma once

#ifdef HAS_FTXUI

#include <ftxui/dom/elements.hpp>
#include "claude/console/AnsiStyle.hpp"

// Internal color palette and helpers shared between FtxuiStreaming.cpp,
// FtxuiRender.cpp, and FtxuiRepl.cpp. NOT part of the public API.

namespace claude::ftxui_colors {

// ========== Macaron color palette (soft pastel tones) ==========
inline const auto MacPeach      = ftxui::Color::RGB(224, 164, 140);  // Brand/primary
inline const auto MacSage       = ftxui::Color::RGB(140, 186, 150);  // Prompt green
inline const auto MacSky        = ftxui::Color::RGB(140, 186, 210);  // Tool cyan/info
inline const auto MacLavender   = ftxui::Color::RGB(180, 160, 210);  // Thinking
inline const auto MacGold       = ftxui::Color::RGB(210, 186, 140);  // List bullets
inline const auto MacRose       = ftxui::Color::RGB(210, 150, 150);  // Error red
inline const auto MacMint       = ftxui::Color::RGB(160, 210, 180);  // Success green
inline const auto MacLilac      = ftxui::Color::RGB(190, 170, 220);  // Magenta accents
inline const auto MacCream      = ftxui::Color::RGB(200, 195, 180);  // Dim text
inline const auto MacShadow     = ftxui::Color::RGB(80, 80, 95);     // Very dim
inline const auto MacBgDark     = ftxui::Color::RGB(30, 30, 42);     // Background

// Context bar threshold colors
inline const auto MacContextOk   = ftxui::Color::RGB(160, 210, 180);  // <70% — matches MacMint
inline const auto MacContextWarn = ftxui::Color::RGB(210, 186, 140);  // 70-85% — matches MacGold
inline const auto MacContextCrit = ftxui::Color::RGB(210, 150, 150);  // >=85% — matches MacRose

// Per-tool colors — delegates to shared AnsiStyle mapping
inline ftxui::Color toolFgColor(const String& toolName) {
    int r, g, b;
    AnsiStyle::toolFgRGB(toolName, r, g, b);
    return ftxui::Color::RGB(r, g, b);
}

inline ftxui::Color toolBgColor(const String& toolName) {
    int r, g, b;
    AnsiStyle::toolBgRGB(toolName, r, g, b);
    return ftxui::Color::RGB(r, g, b);
}

} // namespace claude::ftxui_colors

#endif // HAS_FTXUI
