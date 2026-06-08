#pragma once


#include "../core/Types.hpp"

#include <string>

namespace claude {

/// ANSI 颜色和样式常量
struct AnsiStyle {
    // 重置
    static constexpr const char* RESET = "\033[0m";

    // 前景色
    static constexpr const char* BLACK = "\033[30m";
    static constexpr const char* RED = "\033[31m";
    static constexpr const char* GREEN = "\033[32m";
    static constexpr const char* YELLOW = "\033[33m";
    static constexpr const char* BLUE = "\033[34m";
    static constexpr const char* MAGENTA = "\033[35m";
    static constexpr const char* CYAN = "\033[36m";
    static constexpr const char* WHITE = "\033[37m";

    // 亮色
    static constexpr const char* BRIGHT_BLACK = "\033[90m";
    static constexpr const char* BRIGHT_RED = "\033[91m";
    static constexpr const char* BRIGHT_GREEN = "\033[92m";
    static constexpr const char* BRIGHT_YELLOW = "\033[93m";
    static constexpr const char* BRIGHT_BLUE = "\033[94m";
    static constexpr const char* BRIGHT_MAGENTA = "\033[95m";
    static constexpr const char* BRIGHT_CYAN = "\033[96m";
    static constexpr const char* BRIGHT_WHITE = "\033[97m";

    // 样式
    static constexpr const char* BOLD = "\033[1m";
    static constexpr const char* DIM = "\033[2m";
    static constexpr const char* ITALIC = "\033[3m";
    static constexpr const char* UNDERLINE = "\033[4m";
    static constexpr const char* BLINK = "\033[5m";
    static constexpr const char* REVERSE = "\033[7m";

    // 光标控制
    static constexpr const char* SAVE_CURSOR = "\033[s";
    static constexpr const char* RESTORE_CURSOR = "\033[u";
    static constexpr const char* CLEAR_LINE = "\033[2K";
    static constexpr const char* CLEAR_SCREEN = "\033[2J";

    // 光标显示控制
    static constexpr const char* HIDE_CURSOR = "\033[?25l";
    static constexpr const char* SHOW_CURSOR = "\033[?25h";

    // 行/屏幕擦除
    static constexpr const char* ERASE_LINE_TO_END = "\033[0K";
    static constexpr const char* ERASE_LINE_TO_START = "\033[1K";
    static constexpr const char* ERASE_TO_END = "\033[0J";
    static constexpr const char* ERASE_TO_START = "\033[1J";

    // 滚动
    static constexpr const char* SCROLL_UP = "\033[1S";
    static constexpr const char* SCROLL_DOWN = "\033[1T";

    // Assistant prefix character — ⏺ (U+23FA) on macOS, ● (U+25CF) on Linux/Windows
    // Matches TS Claude Code layout: width-2 column, right-aligned before every assistant text block
#ifdef __APPLE__
    static constexpr const char* ASSISTANT_PREFIX = "\xe2\x8f\xba";  // ⏺ U+23FA
#else
    static constexpr const char* ASSISTANT_PREFIX = "\xe2\x97\x8f";  // ● U+25CF
#endif

    // 语义化颜色 — 匹配原版 Claude Code TypeScript 设计
    struct Semantic {
        // 用户提示符: bold green ❯
        static constexpr const char* PROMPT = "\033[1;32m";
        // 助手指示: bold cyan ●
        static constexpr const char* ASSISTANT = "\033[1;36m";
        // 思考块边框: dim magenta
        static constexpr const char* THINKING_BORDER = "\033[2;35m";
        // 思考块内容: dim
        static constexpr const char* THINKING_CONTENT = "\033[2m";
        // 工具成功 ✓
        static constexpr const char* TOOL_SUCCESS = "\033[1;32m";
        // 工具失败 ✗
        static constexpr const char* TOOL_ERROR = "\033[1;31m";
        // 工具取消 ⊘ (dim)
        static constexpr const char* TOOL_CANCELLED = "\033[2m";
        // 工具拒绝 ⊘ (yellow/dim)
        static constexpr const char* TOOL_REJECTED = "\033[2;33m";
        // 工具前缀 ⎿ (dim)
        static constexpr const char* TOOL_PREFIX = "\033[2m";
        // 代码块边框: dim cyan
        static constexpr const char* CODE_BORDER = "\033[2;36m";
        // Diff 添加行
        static constexpr const char* DIFF_ADD = "\033[32m";
        // Diff 删除行
        static constexpr const char* DIFF_REMOVE = "\033[31m";
        // Diff 块头 @@
        static constexpr const char* DIFF_CHUNK = "\033[36m";
        // Diff 文件头 +++/---
        static constexpr const char* DIFF_HEADER = "\033[1;36m";
        // 状态栏: dim
        static constexpr const char* STATUS_DIM = "\033[2m";
        // 上下文使用率 < 50%
        static constexpr const char* CONTEXT_OK = "\033[32m";
        // 上下文使用率 50-80%
        static constexpr const char* CONTEXT_WARN = "\033[33m";
        // 上下文使用率 > 80%
        static constexpr const char* CONTEXT_CRIT = "\033[31m";
    };

    // 辅助函数
    static String color(int r, int g, int b) {
        return "\033[38;2;" + std::to_string(r) + ";" +
               std::to_string(g) + ";" + std::to_string(b) + "m";
    }

    static String bg(int r, int g, int b) {
        return "\033[48;2;" + std::to_string(r) + ";" +
               std::to_string(g) + ";" + std::to_string(b) + "m";
    }

    /// OSC 8 terminal hyperlink — makes text clickable in supporting terminals
    /// Format: ESC]8;;URL ESC\ TEXT ESC]8;; ESC\
    /// For mailto: links, callers should display the email as plain text instead
    static String createHyperlink(const String& url, const String& text) {
        return "\033]8;;" + url + "\033\\" + text + "\033]8;;\033\\";
    }

    // 移动光标
    static String moveCursor(int row, int col) {
        return "\033[" + std::to_string(row) + ";" + std::to_string(col) + "H";
    }

    // 上移
    static String cursorUp(int n = 1) {
        return "\033[" + std::to_string(n) + "A";
    }

    // 下移
    static String cursorDown(int n = 1) {
        return "\033[" + std::to_string(n) + "B";
    }

    /// 工具前景色映射 — 匹配原版 TS 的 per-tool 颜色设计
    static String toolFgColor(const String& toolName) {
        if (toolName == "Read" || toolName == "ReadTool") return "\033[32m";       // green
        if (toolName == "Write" || toolName == "WriteTool") return "\033[33m";     // yellow/orange
        if (toolName == "Edit" || toolName == "EditTool" || toolName == "FileEditTool" || toolName == "Update") return "\033[33m"; // warm
        if (toolName == "Bash" || toolName == "BashTool") return "\033[34m";       // blue
        if (toolName == "Grep" || toolName == "GrepTool" || toolName == "Search") return "\033[36m";       // cyan
        if (toolName == "Glob" || toolName == "GlobTool" || toolName == "Search") return "\033[36m";       // cyan
        if (toolName == "WebFetch" || toolName == "WebFetchTool") return "\033[35m"; // magenta
        if (toolName == "WebSearch" || toolName == "WebSearchTool") return "\033[35m"; // magenta
        if (toolName == "Agent" || toolName == "AgentTool") return "\033[35m";     // pink/magenta
        if (toolName == "LSP" || toolName == "LSPTool" || toolName == "LspTool") return "\033[33m"; // yellow/gold
        if (toolName == "NotebookEdit" || toolName == "NotebookEditTool") return "\033[36m";
        return "\033[37m"; // default white
    }

    /// 工具背景色映射 — 24-bit RGB 暗色背景，匹配原版 TS 的 badge 样式
    static String toolBgColor(const String& toolName) {
        if (toolName == "Read" || toolName == "ReadTool") return "\033[48;2;20;60;20m";        // dark green
        if (toolName == "Write" || toolName == "WriteTool") return "\033[48;2;80;50;10m";      // dark orange
        if (toolName == "Edit" || toolName == "EditTool" || toolName == "FileEditTool" || toolName == "Update") return "\033[48;2;80;55;15m"; // dark warm
        if (toolName == "Bash" || toolName == "BashTool") return "\033[48;2;15;30;70m";        // dark blue
        if (toolName == "Grep" || toolName == "GrepTool" || toolName == "Search") return "\033[48;2;15;60;60m";        // dark cyan
        if (toolName == "Glob" || toolName == "GlobTool" || toolName == "Search") return "\033[48;2;15;60;60m";        // dark cyan
        if (toolName == "WebFetch" || toolName == "WebFetchTool") return "\033[48;2;60;20;60m"; // dark magenta
        if (toolName == "WebSearch" || toolName == "WebSearchTool") return "\033[48;2;60;20;60m"; // dark magenta
        if (toolName == "Agent" || toolName == "AgentTool") return "\033[48;2;70;20;50m";      // dark pink
        if (toolName == "LSP" || toolName == "LSPTool" || toolName == "LspTool") return "\033[48;2;70;60;10m"; // dark gold
        if (toolName == "NotebookEdit" || toolName == "NotebookEditTool") return "\033[48;2;15;60;60m";
        return "\033[48;2;40;40;40m"; // default dark gray
    }

    /// 获取工具 RGB 前景色值（供 FTXUI 层使用）
    static void toolFgRGB(const String& toolName, int& r, int& g, int& b) {
        if (toolName == "Read" || toolName == "ReadTool")           { r=80;  g=200; b=120; }
        else if (toolName == "Write" || toolName == "WriteTool")    { r=220; g=160; b=80;  }
        else if (toolName == "Edit" || toolName == "EditTool" || toolName == "FileEditTool" || toolName == "Update") { r=220; g=150; b=80; }
        else if (toolName == "Bash" || toolName == "BashTool")      { r=100; g=160; b=220; }
        else if (toolName == "Grep" || toolName == "GrepTool" || toolName == "Search")      { r=80;  g=200; b=200; }
        else if (toolName == "Glob" || toolName == "GlobTool" || toolName == "Search")      { r=80;  g=200; b=200; }
        else if (toolName == "WebFetch" || toolName == "WebFetchTool")   { r=180; g=120; b=200; }
        else if (toolName == "WebSearch" || toolName == "WebSearchTool") { r=180; g=120; b=200; }
        else if (toolName == "Agent" || toolName == "AgentTool")    { r=220; g=110; b=160; }
        else if (toolName == "LSP" || toolName == "LSPTool" || toolName == "LspTool") { r=200; g=180; b=60; }
        else if (toolName == "NotebookEdit" || toolName == "NotebookEditTool") { r=80; g=200; b=200; }
        else { r=200; g=200; b=200; }
    }

    /// 获取工具 RGB 背景色值（供 FTXUI 层使用）
    static void toolBgRGB(const String& toolName, int& r, int& g, int& b) {
        if (toolName == "Read" || toolName == "ReadTool")           { r=20;  g=60;  b=20;  }
        else if (toolName == "Write" || toolName == "WriteTool")    { r=80;  g=50;  b=10;  }
        else if (toolName == "Edit" || toolName == "EditTool" || toolName == "FileEditTool" || toolName == "Update") { r=80; g=55; b=15; }
        else if (toolName == "Bash" || toolName == "BashTool")      { r=15;  g=30;  b=70;  }
        else if (toolName == "Grep" || toolName == "GrepTool" || toolName == "Search")      { r=15;  g=60;  b=60;  }
        else if (toolName == "Glob" || toolName == "GlobTool" || toolName == "Search")      { r=15;  g=60;  b=60;  }
        else if (toolName == "WebFetch" || toolName == "WebFetchTool")   { r=60; g=20;  b=60;  }
        else if (toolName == "WebSearch" || toolName == "WebSearchTool") { r=60; g=20;  b=60;  }
        else if (toolName == "Agent" || toolName == "AgentTool")    { r=70;  g=20;  b=50;  }
        else if (toolName == "LSP" || toolName == "LSPTool" || toolName == "LspTool") { r=70; g=60; b=10; }
        else if (toolName == "NotebookEdit" || toolName == "NotebookEditTool") { r=15; g=60; b=60; }
        else { r=40;  g=40;  b=40;  }
    }
};

} // namespace claude
