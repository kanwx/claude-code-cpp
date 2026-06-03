#include <claude/console/ThemeSystem.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace claude::console {

// ========== Theme ==========

String Theme::apply(const String& text, const String& element) const {
    String colorCode;

    if (element == "primary") colorCode = colors.primary;
    else if (element == "secondary") colorCode = colors.secondary;
    else if (element == "accent") colorCode = colors.accent;
    else if (element == "success") colorCode = colors.success;
    else if (element == "warning") colorCode = colors.warning;
    else if (element == "error") colorCode = colors.error;
    else if (element == "info") colorCode = colors.info;
    else if (element == "muted") colorCode = colors.muted;
    else colorCode = colors.text;

    String result = colorCode;

    auto styleIt = elementStyles.find(element);
    if (styleIt != elementStyles.end()) {
        if (styleIt->second.bold) result += AnsiStyle::BOLD;
        if (styleIt->second.italic) result += AnsiStyle::ITALIC;
        if (styleIt->second.underline) result += AnsiStyle::UNDERLINE;
        if (styleIt->second.dim) result += AnsiStyle::DIM;
    }

    return result + text + AnsiStyle::RESET;
}

String Theme::getColor(const String& name) const {
    if (name == "primary") return colors.primary;
    if (name == "secondary") return colors.secondary;
    if (name == "accent") return colors.accent;
    if (name == "success") return colors.success;
    if (name == "warning") return colors.warning;
    if (name == "error") return colors.error;
    if (name == "info") return colors.info;
    if (name == "muted") return colors.muted;
    if (name == "text") return colors.text;
    if (name == "background") return colors.background;
    // Semantic color roles
    if (name == "prompt") return colors.promptColor;
    if (name == "assistant") return colors.assistantColor;
    if (name == "thinking") return colors.thinkingColor;
    if (name == "tool_success") return colors.toolSuccessColor;
    if (name == "tool_error") return colors.toolErrorColor;
    if (name == "code_border") return colors.codeBorderColor;
    if (name == "diff_add") return colors.diffAddColor;
    if (name == "diff_remove") return colors.diffRemoveColor;
    if (name == "diff_chunk") return colors.diffChunkColor;
    if (name == "status_dim") return colors.statusDimColor;
    if (name == "context_ok") return colors.contextOkColor;
    if (name == "context_warn") return colors.contextWarnColor;
    if (name == "context_crit") return colors.contextCritColor;
    return "";
}

// ========== 预定义主题 ==========

namespace themes {

Theme dark() {
    return ThemeBuilder()
        .name("dark")
        .description("Default dark theme")
        .primary("\033[36m")
        .secondary("\033[34m")
        .accent("\033[35m")
        .success("\033[32m")
        .warning("\033[33m")
        .error("\033[31m")
        .info("\033[34m")
        .muted("\033[90m")
        .text("\033[37m")
        .background("\033[40m")
        .promptColor("\033[1;32m")
        .assistantColor("\033[1;36m")
        .thinkingColor("\033[2;35m")
        .toolSuccessColor("\033[1;32m")
        .toolErrorColor("\033[1;31m")
        .codeBorderColor("\033[2;36m")
        .diffAddColor("\033[32m")
        .diffRemoveColor("\033[31m")
        .diffChunkColor("\033[36m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[32m")
        .contextWarnColor("\033[33m")
        .contextCritColor("\033[31m")
        .build();
}

Theme light() {
    return ThemeBuilder()
        .name("light")
        .description("Light theme")
        .primary("\033[34m")
        .secondary("\033[36m")
        .accent("\033[35m")
        .success("\033[32m")
        .warning("\033[33m")
        .error("\033[31m")
        .info("\033[34m")
        .muted("\033[90m")
        .text("\033[30m")
        .background("\033[47m")
        .promptColor("\033[1;32m")
        .assistantColor("\033[1;36m")
        .thinkingColor("\033[2;35m")
        .toolSuccessColor("\033[1;32m")
        .toolErrorColor("\033[1;31m")
        .codeBorderColor("\033[2;36m")
        .diffAddColor("\033[32m")
        .diffRemoveColor("\033[31m")
        .diffChunkColor("\033[36m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[32m")
        .contextWarnColor("\033[33m")
        .contextCritColor("\033[31m")
        .build();
}

Theme monokai() {
    return ThemeBuilder()
        .name("monokai")
        .description("Monokai color scheme")
        .primary("\033[38;2;102;217;239m")    // Cyan
        .secondary("\033[38;2;117;113;94m")   // Comment
        .accent("\033[38;2;249;38;114m")      // Pink
        .success("\033[38;2;166;226;46m")     // Green
        .warning("\033[38;2;253;151;31m")     // Orange
        .error("\033[38;2;249;38;114m")       // Pink
        .info("\033[38;2;102;217;239m")       // Cyan
        .muted("\033[38;2;117;113;94m")       // Comment
        .text("\033[38;2;248;248;242m")       // White
        .background("\033[48;2;39;40;34m")    // Dark gray
        .promptColor("\033[1;38;2;166;226;46m")
        .assistantColor("\033[1;38;2;102;217;239m")
        .thinkingColor("\033[2;38;2;117;113;94m")
        .toolSuccessColor("\033[1;38;2;166;226;46m")
        .toolErrorColor("\033[1;38;2;249;38;114m")
        .codeBorderColor("\033[2;38;2;102;217;239m")
        .diffAddColor("\033[38;2;166;226;46m")
        .diffRemoveColor("\033[38;2;249;38;114m")
        .diffChunkColor("\033[38;2;102;217;239m")
        .statusDimColor("\033[2;38;2;117;113;94m")
        .contextOkColor("\033[38;2;166;226;46m")
        .contextWarnColor("\033[38;2;253;151;31m")
        .contextCritColor("\033[38;2;249;38;114m")
        .build();
}

Theme dracula() {
    return ThemeBuilder()
        .name("dracula")
        .description("Dracula color scheme")
        .primary("\033[38;2;139;233;253m")    // Cyan
        .secondary("\033[38;2;98;114;164m")   // Comment
        .accent("\033[38;2;255;121;198m")     // Pink
        .success("\033[38;2;80;250;123m")     // Green
        .warning("\033[38;2;255;184;108m")    // Orange
        .error("\033[38;2;255;85;85m")        // Red
        .info("\033[38;2;139;233;253m")       // Cyan
        .muted("\033[38;2;98;114;164m")       // Comment
        .text("\033[38;2;248;248;242m")       // White
        .background("\033[48;2;40;42;54m")    // Dark purple
        .promptColor("\033[1;38;2;80;250;123m")
        .assistantColor("\033[1;38;2;139;233;253m")
        .thinkingColor("\033[2;38;2;98;114;164m")
        .toolSuccessColor("\033[1;38;2;80;250;123m")
        .toolErrorColor("\033[1;38;2;255;85;85m")
        .codeBorderColor("\033[2;38;2;139;233;253m")
        .diffAddColor("\033[38;2;80;250;123m")
        .diffRemoveColor("\033[38;2;255;85;85m")
        .diffChunkColor("\033[38;2;139;233;253m")
        .statusDimColor("\033[2;38;2;98;114;164m")
        .contextOkColor("\033[38;2;80;250;123m")
        .contextWarnColor("\033[38;2;255;184;108m")
        .contextCritColor("\033[38;2;255;85;85m")
        .build();
}

Theme nord() {
    return ThemeBuilder()
        .name("nord")
        .description("Nord color scheme")
        .primary("\033[38;2;136;192;208m")    // Frost cyan
        .secondary("\033[38;2;94;129;172m")   // Frost blue
        .accent("\033[38;2;180;142;173m")     // Aurora purple
        .success("\033[38;2;163;190;140m")    // Aurora green
        .warning("\033[38;2;235;203;139m")    // Aurora yellow
        .error("\033[38;2;191;97;106m")       // Aurora red
        .info("\033[38;2;136;192;208m")       // Frost cyan
        .muted("\033[38;2;76;86;106m")        // Polar night
        .text("\033[38;2;236;239;244m")       // Snow storm
        .background("\033[48;2;46;52;64m")    // Polar night
        .promptColor("\033[1;38;2;163;190;140m")
        .assistantColor("\033[1;38;2;136;192;208m")
        .thinkingColor("\033[2;38;2;76;86;106m")
        .toolSuccessColor("\033[1;38;2;163;190;140m")
        .toolErrorColor("\033[1;38;2;191;97;106m")
        .codeBorderColor("\033[2;38;2;136;192;208m")
        .diffAddColor("\033[38;2;163;190;140m")
        .diffRemoveColor("\033[38;2;191;97;106m")
        .diffChunkColor("\033[38;2;136;192;208m")
        .statusDimColor("\033[2;38;2;76;86;106m")
        .contextOkColor("\033[38;2;163;190;140m")
        .contextWarnColor("\033[38;2;235;203;139m")
        .contextCritColor("\033[38;2;191;97;106m")
        .build();
}

Theme solarizedDark() {
    return ThemeBuilder()
        .name("solarized-dark")
        .description("Solarized dark theme")
        .primary("\033[38;2;38;139;210m")     // Blue
        .secondary("\033[38;2;42;161;152m")   // Cyan
        .accent("\033[38;2;211;54;130m")      // Magenta
        .success("\033[38;2;133;153;0m")      // Green
        .warning("\033[38;2;181;137;0m")      // Yellow
        .error("\033[38;2;220;50;47m")        // Red
        .info("\033[38;2;38;139;210m")        // Blue
        .muted("\033[38;2;101;123;131m")      // Base01
        .text("\033[38;2;253;246;227m")       // Base3
        .background("\033[48;2;7;54;66m")     // Base02
        .promptColor("\033[1;38;2;133;153;0m")
        .assistantColor("\033[1;38;2;42;161;152m")
        .thinkingColor("\033[2;38;2;101;123;131m")
        .toolSuccessColor("\033[1;38;2;133;153;0m")
        .toolErrorColor("\033[1;38;2;220;50;47m")
        .codeBorderColor("\033[2;38;2;42;161;152m")
        .diffAddColor("\033[38;2;133;153;0m")
        .diffRemoveColor("\033[38;2;220;50;47m")
        .diffChunkColor("\033[38;2;42;161;152m")
        .statusDimColor("\033[2;38;2;101;123;131m")
        .contextOkColor("\033[38;2;133;153;0m")
        .contextWarnColor("\033[38;2;181;137;0m")
        .contextCritColor("\033[38;2;220;50;47m")
        .build();
}

Theme solarizedLight() {
    return ThemeBuilder()
        .name("solarized-light")
        .description("Solarized light theme")
        .primary("\033[38;2;38;139;210m")     // Blue
        .secondary("\033[38;2;42;161;152m")   // Cyan
        .accent("\033[38;2;211;54;130m")      // Magenta
        .success("\033[38;2;133;153;0m")      // Green
        .warning("\033[38;2;181;137;0m")      // Yellow
        .error("\033[38;2;220;50;47m")        // Red
        .info("\033[38;2;38;139;210m")        // Blue
        .muted("\033[38;2;147;161;161m")      // Base1
        .text("\033[38;2;7;54;66m")           // Base02
        .background("\033[48;2;253;246;227m") // Base3
        .promptColor("\033[1;38;2;133;153;0m")
        .assistantColor("\033[1;38;2;42;161;152m")
        .thinkingColor("\033[2;38;2;147;161;161m")
        .toolSuccessColor("\033[1;38;2;133;153;0m")
        .toolErrorColor("\033[1;38;2;220;50;47m")
        .codeBorderColor("\033[2;38;2;42;161;152m")
        .diffAddColor("\033[38;2;133;153;0m")
        .diffRemoveColor("\033[38;2;220;50;47m")
        .diffChunkColor("\033[38;2;42;161;152m")
        .statusDimColor("\033[2;38;2;147;161;161m")
        .contextOkColor("\033[38;2;133;153;0m")
        .contextWarnColor("\033[38;2;181;137;0m")
        .contextCritColor("\033[38;2;220;50;47m")
        .build();
}

Theme oneDark() {
    return ThemeBuilder()
        .name("one-dark")
        .description("Atom One Dark theme")
        .primary("\033[38;2;97;175;239m")     // Blue
        .secondary("\033[38;2;152;195;121m")  // Green
        .accent("\033[38;2;198;120;221m")     // Purple
        .success("\033[38;2;152;195;121m")    // Green
        .warning("\033[38;2;229;192;123m")    // Yellow
        .error("\033[38;2;224;108;117m")      // Red
        .info("\033[38;2;86;156;214m")        // Cyan
        .muted("\033[38;2;92;99;112m")        // Comment
        .text("\033[38;2;171;178;191m")       // White
        .background("\033[48;2;40;44;52m")    // Background
        .promptColor("\033[1;38;2;152;195;121m")
        .assistantColor("\033[1;38;2;86;156;214m")
        .thinkingColor("\033[2;38;2;92;99;112m")
        .toolSuccessColor("\033[1;38;2;152;195;121m")
        .toolErrorColor("\033[1;38;2;224;108;117m")
        .codeBorderColor("\033[2;38;2;86;156;214m")
        .diffAddColor("\033[38;2;152;195;121m")
        .diffRemoveColor("\033[38;2;224;108;117m")
        .diffChunkColor("\033[38;2;86;156;214m")
        .statusDimColor("\033[2;38;2;92;99;112m")
        .contextOkColor("\033[38;2;152;195;121m")
        .contextWarnColor("\033[38;2;229;192;123m")
        .contextCritColor("\033[38;2;224;108;117m")
        .build();
}

Theme gruvbox() {
    return ThemeBuilder()
        .name("gruvbox")
        .description("Gruvbox color scheme")
        .primary("\033[38;2;104;157;106m")    // Aqua
        .secondary("\033[38;2;230;101;40m")   // Orange
        .accent("\033[38;2;211;134;155m")     // Purple
        .success("\033[38;2;184;187;38m")     // Green
        .warning("\033[38;2;250;189;47m")     // Yellow
        .error("\033[38;2;251;73;52m")        // Red
        .info("\033[38;2;142;192;124m")       // Blue
        .muted("\033[38;2;168;153;132m")      // Gray
        .text("\033[38;2;235;219;179m")       // White
        .background("\033[48;2;40;40;40m")    // Background
        .promptColor("\033[1;38;2;184;187;38m")
        .assistantColor("\033[1;38;2;142;192;124m")
        .thinkingColor("\033[2;38;2;168;153;132m")
        .toolSuccessColor("\033[1;38;2;184;187;38m")
        .toolErrorColor("\033[1;38;2;251;73;52m")
        .codeBorderColor("\033[2;38;2;142;192;124m")
        .diffAddColor("\033[38;2;184;187;38m")
        .diffRemoveColor("\033[38;2;251;73;52m")
        .diffChunkColor("\033[38;2;142;192;124m")
        .statusDimColor("\033[2;38;2;168;153;132m")
        .contextOkColor("\033[38;2;184;187;38m")
        .contextWarnColor("\033[38;2;250;189;47m")
        .contextCritColor("\033[38;2;251;73;52m")
        .build();
}

Theme github() {
    return ThemeBuilder()
        .name("github")
        .description("GitHub color scheme")
        .primary("\033[38;2;9;105;218m")      // Blue
        .secondary("\033[38;2;63;185;80m")    // Green
        .accent("\033[38;2;163;113;247m")     // Purple
        .success("\033[38;2;63;185;80m")      // Green
        .warning("\033[38;2;255;140;0m")      // Orange
        .error("\033[38;2;218;54;51m")        // Red
        .info("\033[38;2;9;105;218m")         // Blue
        .muted("\033[38;2;110;118;129m")      // Gray
        .text("\033[38;2;36;41;46m")          // Black
        .background("\033[48;2;255;255;255m") // White
        .promptColor("\033[1;38;2;63;185;80m")
        .assistantColor("\033[1;38;2;9;105;218m")
        .thinkingColor("\033[2;38;2;110;118;129m")
        .toolSuccessColor("\033[1;38;2;63;185;80m")
        .toolErrorColor("\033[1;38;2;218;54;51m")
        .codeBorderColor("\033[2;38;2;9;105;218m")
        .diffAddColor("\033[38;2;63;185;80m")
        .diffRemoveColor("\033[38;2;218;54;51m")
        .diffChunkColor("\033[38;2;9;105;218m")
        .statusDimColor("\033[2;38;2;110;118;129m")
        .contextOkColor("\033[38;2;63;185;80m")
        .contextWarnColor("\033[38;2;255;140;0m")
        .contextCritColor("\033[38;2;218;54;51m")
        .build();
}

Theme highContrast() {
    return ThemeBuilder()
        .name("high-contrast")
        .description("High contrast theme for accessibility")
        .primary("\033[36m")
        .secondary("\033[34m")
        .accent("\033[35m")
        .success("\033[32m")
        .warning("\033[33m")
        .error("\033[31m")
        .info("\033[34m")
        .muted("\033[37m")
        .text("\033[97m")
        .background("\033[40m")
        .promptColor("\033[1;32m")
        .assistantColor("\033[1;36m")
        .thinkingColor("\033[2;37m")
        .toolSuccessColor("\033[1;32m")
        .toolErrorColor("\033[1;31m")
        .codeBorderColor("\033[2;36m")
        .diffAddColor("\033[32m")
        .diffRemoveColor("\033[31m")
        .diffChunkColor("\033[36m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[32m")
        .contextWarnColor("\033[33m")
        .contextCritColor("\033[31m")
        .build();
}

Theme darkDaltonized() {
    return ThemeBuilder()
        .name("dark-daltonized")
        .description("Dark theme with color-blind friendly cyan/orange instead of green/red")
        .primary("\033[36m").secondary("\033[34m").accent("\033[35m")
        .success("\033[36m")       // cyan instead of green
        .warning("\033[33m")
        .error("\033[38;5;208m")   // orange instead of red
        .info("\033[34m").muted("\033[90m").text("\033[37m").background("\033[40m")
        .promptColor("\033[1;36m")     // cyan
        .assistantColor("\033[1;36m")
        .thinkingColor("\033[2;35m")
        .toolSuccessColor("\033[1;36m") // cyan
        .toolErrorColor("\033[1;38;5;208m") // orange
        .codeBorderColor("\033[2;36m")
        .diffAddColor("\033[36m")       // cyan
        .diffRemoveColor("\033[38;5;208m") // orange
        .diffChunkColor("\033[33m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[36m")     // cyan
        .contextWarnColor("\033[33m")
        .contextCritColor("\033[38;5;208m") // orange
        .build();
}

Theme lightDaltonized() {
    return ThemeBuilder()
        .name("light-daltonized")
        .description("Light theme with color-blind friendly cyan/orange")
        .primary("\033[34m").secondary("\033[36m").accent("\033[35m")
        .success("\033[36m")       // cyan
        .warning("\033[33m")
        .error("\033[38;5;208m")   // orange
        .info("\033[34m").muted("\033[90m").text("\033[30m").background("\033[47m")
        .promptColor("\033[1;36m")
        .assistantColor("\033[1;34m")
        .thinkingColor("\033[2;35m")
        .toolSuccessColor("\033[1;36m")
        .toolErrorColor("\033[1;38;5;208m")
        .codeBorderColor("\033[2;36m")
        .diffAddColor("\033[36m")
        .diffRemoveColor("\033[38;5;208m")
        .diffChunkColor("\033[33m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[36m")
        .contextWarnColor("\033[33m")
        .contextCritColor("\033[38;5;208m")
        .build();
}

Theme darkAnsi() {
    return ThemeBuilder()
        .name("dark-ansi")
        .description("Dark theme limited to ANSI 16 colors for terminal compatibility")
        .primary("\033[36m").secondary("\033[34m").accent("\033[35m")
        .success("\033[32m").warning("\033[33m").error("\033[31m")
        .info("\033[34m").muted("\033[90m").text("\033[37m").background("\033[40m")
        .promptColor("\033[1;32m").assistantColor("\033[1;36m")
        .thinkingColor("\033[2;35m").toolSuccessColor("\033[1;32m")
        .toolErrorColor("\033[1;31m").codeBorderColor("\033[2;36m")
        .diffAddColor("\033[32m").diffRemoveColor("\033[31m").diffChunkColor("\033[36m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[32m").contextWarnColor("\033[33m")
        .contextCritColor("\033[31m")
        .build();
}

Theme lightAnsi() {
    return ThemeBuilder()
        .name("light-ansi")
        .description("Light theme limited to ANSI 16 colors")
        .primary("\033[34m").secondary("\033[36m").accent("\033[35m")
        .success("\033[32m").warning("\033[33m").error("\033[31m")
        .info("\033[34m").muted("\033[90m").text("\033[30m").background("\033[47m")
        .promptColor("\033[1;32m").assistantColor("\033[1;34m")
        .thinkingColor("\033[2;35m").toolSuccessColor("\033[1;32m")
        .toolErrorColor("\033[1;31m").codeBorderColor("\033[2;36m")
        .diffAddColor("\033[32m").diffRemoveColor("\033[31m").diffChunkColor("\033[36m")
        .statusDimColor("\033[2m")
        .contextOkColor("\033[32m").contextWarnColor("\033[33m")
        .contextCritColor("\033[31m")
        .build();
}

}

// ========== ThemeManager ==========

ThemeManager::ThemeManager() {
    loadBuiltinThemes();
}

void ThemeManager::initialize() {
    loadBuiltinThemes();
    loadUserThemes();

    // Load saved preference
    const char* home = std::getenv("HOME");
    if (home) {
        std::ifstream file(String(home) + "/.claude/theme.json");
        if (file) {
            try {
                std::stringstream buffer;
                buffer << file.rdbuf();
                Json config = Json::parse(buffer.str());
                String savedTheme = config.value("current", "dark");
                setTheme(savedTheme);
            } catch (...) {}
        }
    }
}

bool ThemeManager::setTheme(const String& name) {
    auto it = themes_.find(name);
    if (it == themes_.end()) return false;

    currentTheme_ = it->second;
    return true;
}

void ThemeManager::registerTheme(const Theme& theme) {
    themes_[theme.name] = theme;
}

std::optional<Theme> ThemeManager::getTheme(const String& name) const {
    auto it = themes_.find(name);
    if (it == themes_.end()) return std::nullopt;
    return it->second;
}

std::vector<String> ThemeManager::listThemes() const {
    std::vector<String> result;
    for (const auto& [name, theme] : themes_) {
        result.push_back(name);
    }
    return result;
}

bool ThemeManager::loadTheme(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return false;

    try {
        std::stringstream buffer;
        buffer << file.rdbuf();
        Json j = Json::parse(buffer.str());

        Theme theme;
        theme.name = j.value("name", "");
        theme.description = j.value("description", "");
        theme.colors.primary = j.value("primary", "\033[36m");
        theme.colors.secondary = j.value("secondary", "\033[34m");
        theme.colors.accent = j.value("accent", "\033[35m");
        theme.colors.success = j.value("success", "\033[32m");
        theme.colors.warning = j.value("warning", "\033[33m");
        theme.colors.error = j.value("error", "\033[31m");
        theme.colors.info = j.value("info", "\033[34m");
        theme.colors.muted = j.value("muted", "\033[90m");
        theme.colors.text = j.value("text", "\033[37m");
        theme.colors.background = j.value("background", "\033[40m");

        if (!theme.name.empty()) {
            themes_[theme.name] = theme;
            return true;
        }
    } catch (...) {}

    return false;
}

bool ThemeManager::saveConfig() const {
    const char* home = std::getenv("HOME");
    if (!home) return false;

    std::filesystem::path configPath = String(home) + "/.claude";
    std::filesystem::create_directories(configPath);

    Json config = {{"current", currentTheme_.name}};

    std::ofstream file(configPath / "theme.json");
    if (!file) return false;

    file << config.dump(2);
    return true;
}

String ThemeManager::apply(const String& text, const String& element) const {
    return currentTheme_.apply(text, element);
}

String ThemeManager::color(const String& name) const {
    return currentTheme_.getColor(name);
}

String ThemeManager::semanticColor(const String& role) const {
    // Try theme-specific semantic color first
    String c = currentTheme_.getColor(role);
    if (!c.empty()) return c;
    // Fallback to AnsiStyle::Semantic defaults
    if (role == "prompt") return AnsiStyle::Semantic::PROMPT;
    if (role == "assistant") return AnsiStyle::Semantic::ASSISTANT;
    if (role == "thinking") return AnsiStyle::Semantic::THINKING_BORDER;
    if (role == "tool_success") return AnsiStyle::Semantic::TOOL_SUCCESS;
    if (role == "tool_error") return AnsiStyle::Semantic::TOOL_ERROR;
    if (role == "code_border") return AnsiStyle::Semantic::CODE_BORDER;
    if (role == "diff_add") return AnsiStyle::Semantic::DIFF_ADD;
    if (role == "diff_remove") return AnsiStyle::Semantic::DIFF_REMOVE;
    if (role == "diff_chunk") return AnsiStyle::Semantic::DIFF_CHUNK;
    if (role == "status_dim") return AnsiStyle::Semantic::STATUS_DIM;
    if (role == "context_ok") return AnsiStyle::Semantic::CONTEXT_OK;
    if (role == "context_warn") return AnsiStyle::Semantic::CONTEXT_WARN;
    if (role == "context_crit") return AnsiStyle::Semantic::CONTEXT_CRIT;
    return "";
}

String ThemeManager::toolColor(const String& toolName, bool bg) const {
    return bg ? AnsiStyle::toolBgColor(toolName) : AnsiStyle::toolFgColor(toolName);
}

String ThemeManager::format(const String& text, const String& style) const {
    return apply(text, style);
}

void ThemeManager::loadBuiltinThemes() {
    registerTheme(themes::dark());
    registerTheme(themes::light());
    registerTheme(themes::monokai());
    registerTheme(themes::dracula());
    registerTheme(themes::nord());
    registerTheme(themes::solarizedDark());
    registerTheme(themes::solarizedLight());
    registerTheme(themes::oneDark());
    registerTheme(themes::gruvbox());
    registerTheme(themes::github());
    registerTheme(themes::highContrast());
    registerTheme(themes::darkDaltonized());
    registerTheme(themes::lightDaltonized());
    registerTheme(themes::darkAnsi());
    registerTheme(themes::lightAnsi());

    currentTheme_ = themes::dark();
}

void ThemeManager::loadUserThemes() {
    const char* home = std::getenv("HOME");
    if (!home) return;

    std::filesystem::path themesPath = String(home) + "/.claude/themes";
    if (!std::filesystem::exists(themesPath)) return;

    for (const auto& entry : std::filesystem::directory_iterator(themesPath)) {
        if (entry.path().extension() == ".json") {
            loadTheme(entry.path());
        }
    }
}

} // namespace claude::console
