#pragma once

#include "../core/Types.hpp"
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace claude::console {

using Json = nlohmann::json;

/// 主题颜色定义
struct ThemeColors {
    String primary;        // 主色调
    String secondary;      // 次色调
    String accent;         // 强调色
    String success;        // 成功色
    String warning;        // 警告色
    String error;          // 错误色
    String info;           // 信息色
    String muted;          // 淡色
    String text;           // 文本色
    String background;     // 背景色
    // 语义化颜色 — 匹配原版 Claude Code UI 角色
    String promptColor;          // 用户提示符 ❯ (bold green)
    String assistantColor;       // 助手指示 ● (bold cyan)
    String thinkingColor;        // 思考块边框 (dim magenta)
    String toolSuccessColor;     // 工具成功 ✓
    String toolErrorColor;       // 工具失败 ✗
    String codeBorderColor;      // 代码块边框 (dim cyan)
    String diffAddColor;         // Diff 添加行
    String diffRemoveColor;      // Diff 删除行
    String diffChunkColor;       // Diff 块头 @@
    String statusDimColor;       // 状态栏 dim
    String contextOkColor;       // 上下文使用率 < 50%
    String contextWarnColor;     // 上下文使用率 50-80%
    String contextCritColor;     // 上下文使用率 > 80%
};

/// 主题样式定义
struct ThemeStyles {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool dim = false;
};

/// 完整主题定义
struct Theme {
    String name;
    String description;
    ThemeColors colors;
    std::unordered_map<String, ThemeStyles> elementStyles;
    Json metadata;

    /// 应用主题到文本
    String apply(const String& text, const String& element = "text") const;

    /// 获取颜色
    String getColor(const String& name) const;
};

/// 预定义主题
namespace themes {

/// 默认主题
Theme dark();

/// 浅色主题
Theme light();

/// Monokai主题
Theme monokai();

/// Dracula主题
Theme dracula();

/// Nord主题
Theme nord();

/// Solarized Dark主题
Theme solarizedDark();

/// Solarized Light主题
Theme solarizedLight();

/// One Dark主题
Theme oneDark();

/// Gruvbox主题
Theme gruvbox();

/// GitHub主题
Theme github();

/// 高对比度主题
Theme highContrast();

/// Dark daltonized (color-blind friendly: cyan/orange instead of green/red)
Theme darkDaltonized();

/// Light daltonized (color-blind friendly: cyan/orange)
Theme lightDaltonized();

/// Dark theme limited to ANSI 16 colors
Theme darkAnsi();

/// Light theme limited to ANSI 16 colors
Theme lightAnsi();

}

/// 主题管理器
class ThemeManager {
public:
    static ThemeManager& instance() {
        static ThemeManager manager;
        return manager;
    }

    /// 初始化
    void initialize();

    /// 获取当前主题
    Theme getCurrentTheme() const { return currentTheme_; }

    /// 设置主题
    bool setTheme(const String& name);

    /// 注册主题
    void registerTheme(const Theme& theme);

    /// 获取主题
    std::optional<Theme> getTheme(const String& name) const;

    /// 列出所有主题
    std::vector<String> listThemes() const;

    /// 从文件加载主题
    bool loadTheme(const std::filesystem::path& path);

    /// 保存当前主题配置
    bool saveConfig() const;

    /// 应用当前主题到文本
    String apply(const String& text, const String& element = "text") const;

    /// 获取颜色
    String color(const String& name) const;

    /// 获取语义化颜色 — 匹配原版 Claude Code UI 角色
    /// @param role "prompt", "assistant", "thinking", "tool_success", "tool_error",
    ///             "code_border", "diff_add", "diff_remove", "diff_chunk",
    ///             "status_dim", "context_ok", "context_warn", "context_crit"
    String semanticColor(const String& role) const;

    /// 获取工具颜色
    /// @param toolName 工具名称 (e.g., "Read", "Bash")
    /// @param bg true 返回背景色, false 返回前景色
    String toolColor(const String& toolName, bool bg = false) const;

    /// 格式化文本
    String format(const String& text, const String& style) const;

private:
    ThemeManager();

    Theme currentTheme_;
    std::unordered_map<String, Theme> themes_;

    void loadBuiltinThemes();
    void loadUserThemes();
};

/// 主题构建器
class ThemeBuilder {
public:
    ThemeBuilder& name(const String& name) { theme_.name = name; return *this; }
    ThemeBuilder& description(const String& desc) { theme_.description = desc; return *this; }
    ThemeBuilder& primary(const String& color) { theme_.colors.primary = color; return *this; }
    ThemeBuilder& secondary(const String& color) { theme_.colors.secondary = color; return *this; }
    ThemeBuilder& accent(const String& color) { theme_.colors.accent = color; return *this; }
    ThemeBuilder& success(const String& color) { theme_.colors.success = color; return *this; }
    ThemeBuilder& warning(const String& color) { theme_.colors.warning = color; return *this; }
    ThemeBuilder& error(const String& color) { theme_.colors.error = color; return *this; }
    ThemeBuilder& info(const String& color) { theme_.colors.info = color; return *this; }
    ThemeBuilder& muted(const String& color) { theme_.colors.muted = color; return *this; }
    ThemeBuilder& text(const String& color) { theme_.colors.text = color; return *this; }
    ThemeBuilder& background(const String& color) { theme_.colors.background = color; return *this; }
    ThemeBuilder& promptColor(const String& color) { theme_.colors.promptColor = color; return *this; }
    ThemeBuilder& assistantColor(const String& color) { theme_.colors.assistantColor = color; return *this; }
    ThemeBuilder& thinkingColor(const String& color) { theme_.colors.thinkingColor = color; return *this; }
    ThemeBuilder& toolSuccessColor(const String& color) { theme_.colors.toolSuccessColor = color; return *this; }
    ThemeBuilder& toolErrorColor(const String& color) { theme_.colors.toolErrorColor = color; return *this; }
    ThemeBuilder& codeBorderColor(const String& color) { theme_.colors.codeBorderColor = color; return *this; }
    ThemeBuilder& diffAddColor(const String& color) { theme_.colors.diffAddColor = color; return *this; }
    ThemeBuilder& diffRemoveColor(const String& color) { theme_.colors.diffRemoveColor = color; return *this; }
    ThemeBuilder& diffChunkColor(const String& color) { theme_.colors.diffChunkColor = color; return *this; }
    ThemeBuilder& statusDimColor(const String& color) { theme_.colors.statusDimColor = color; return *this; }
    ThemeBuilder& contextOkColor(const String& color) { theme_.colors.contextOkColor = color; return *this; }
    ThemeBuilder& contextWarnColor(const String& color) { theme_.colors.contextWarnColor = color; return *this; }
    ThemeBuilder& contextCritColor(const String& color) { theme_.colors.contextCritColor = color; return *this; }

    Theme build() { return theme_; }

private:
    Theme theme_;
};

} // namespace claude::console
