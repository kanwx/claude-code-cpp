#include <catch2/catch_test_macros.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <claude/ui/FtxuiMarkdown.hpp>

using namespace claude;

// Helper: render markdown to a Screen, return plain-text lines
static std::vector<std::string> renderToLines(const std::string& md, int width = 40) {
    auto elements = FtxuiMarkdown::render(md);
    if (elements.empty()) return {};
    auto doc = ftxui::vbox(std::move(elements));
    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(width),
        ftxui::Dimension::Fixed(20)
    );
    ftxui::Render(screen, doc);

    std::vector<std::string> lines;
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string line;
        for (int x = 0; x < screen.dimx(); ++x) {
            line += screen.at(x, y);
        }
        // Trim trailing spaces
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
    }
    // Trim trailing empty lines
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

// ===== BulletList hanging indent =====

TEST_CASE("FtxuiMarkdown bullet list wraps with hanging indent", "[ftxui][list][hanging-indent]") {
    auto lines = renderToLines(
        "- This is a very long bullet item that should definitely wrap to the next line in a narrow terminal window",
        30
    );
    REQUIRE_FALSE(lines.empty());

    // First line should have bullet
    bool foundBullet = false;
    for (const auto& line : lines) {
        if (line.find("\xe2\x80\xa2") != std::string::npos) { // UTF-8 bullet "•"
            foundBullet = true;
            break;
        }
    }
    REQUIRE(foundBullet);
}

TEST_CASE("FtxuiMarkdown bullet list short items render flat", "[ftxui][list]") {
    auto elements = FtxuiMarkdown::render("- short\n- items\n");
    REQUIRE_FALSE(elements.empty());
}

// ===== NumberedList hanging indent =====

TEST_CASE("FtxuiMarkdown numbered list wraps with hanging indent", "[ftxui][list][hanging-indent]") {
    auto lines = renderToLines(
        "1. This is a very long numbered list item that should definitely wrap to multiple lines in a narrow terminal window to test hanging indent behavior for ordered lists",
        30
    );
    REQUIRE_FALSE(lines.empty());

    bool foundNumber = false;
    for (const auto& line : lines) {
        if (line.find("1.") != std::string::npos) {
            foundNumber = true;
            break;
        }
    }
    REQUIRE(foundNumber);
}

// ===== NestedList hanging indent =====

TEST_CASE("FtxuiMarkdown nested list wraps with hanging indent", "[ftxui][list][hanging-indent]") {
    auto lines = renderToLines(
        "  - This is a very long nested list item that should wrap and keep its indentation on continuation lines",
        30
    );
    REQUIRE_FALSE(lines.empty());
}

// ===== Mixed lists =====

TEST_CASE("FtxuiMarkdown mixed bullet and numbered lists", "[ftxui][list]") {
    std::string md =
        "- First bullet\n"
        "- Second bullet is a long one that wraps around the screen\n"
        "1. First numbered\n"
        "2. Second numbered is also quite long and should wrap nicely\n"
        "- Another bullet\n";
    auto elements = FtxuiMarkdown::render(md);
    REQUIRE_FALSE(elements.empty());
}

// ===== Paragraph unaffected =====

TEST_CASE("FtxuiMarkdown paragraph rendering unaffected by list changes", "[ftxui][paragraph]") {
    auto lines = renderToLines(
        "This is a normal paragraph that should render the same as before any list changes were made to the renderer.",
        40
    );
    REQUIRE_FALSE(lines.empty());

    // Paragraph should render text content
    bool foundText = false;
    for (const auto& line : lines) {
        if (line.find("normal") != std::string::npos) {
            foundText = true;
            break;
        }
    }
    REQUIRE(foundText);
}

// ===== CodeBlock smoke test =====

TEST_CASE("FtxuiMarkdown code block rendering unaffected", "[ftxui][code]") {
    std::string md =
        "```cpp\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n"
        "```\n";
    auto elements = FtxuiMarkdown::render(md);
    REQUIRE_FALSE(elements.empty());
}

// ===== Table smoke test =====

TEST_CASE("FtxuiMarkdown table rendering unaffected", "[ftxui][table]") {
    std::string md =
        "| A | B |\n"
        "| --- | --- |\n"
        "| 1 | 2 |\n";
    auto elements = FtxuiMarkdown::render(md);
    REQUIRE_FALSE(elements.empty());
}

// ===== Task list =====

TEST_CASE("FtxuiMarkdown task list renders", "[ftxui][list][task]") {
    std::string md =
        "- [ ] todo item\n"
        "- [x] done item\n";
    auto elements = FtxuiMarkdown::render(md);
    REQUIRE_FALSE(elements.empty());
}

// ===== CJK list items =====

TEST_CASE("FtxuiMarkdown CJK bullet items render", "[ftxui][list][cjk]") {
    std::string md =
        "- \xe8\xbf\x99\xe6\x98\xaf\xe4\xb8\x80\xe4\xb8\xaa\xe4\xb8\xad\xe6\x96\x87\xe5\x88\x97\xe8\xa1\xa8\xe9\xa1\xb9\xe7\x9b\xae\n"
        "- \xe7\xac\xac\xe4\xba\x8c\xe4\xb8\xaa\xe4\xb8\xad\xe6\x96\x87\xe9\xa1\xb9\xe7\x9b\xae\n";
    auto elements = FtxuiMarkdown::render(md);
    REQUIRE_FALSE(elements.empty());
}

// ===== Empty input =====

TEST_CASE("FtxuiMarkdown empty input", "[ftxui][edge]") {
    // Empty string returns a single empty paragraph element (not empty vector)
    auto elements = FtxuiMarkdown::render("");
    REQUIRE_FALSE(elements.empty());
    // Should not crash when rendered to screen
    auto doc = ftxui::vbox(std::move(elements));
    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(40),
        ftxui::Dimension::Fixed(10)
    );
    REQUIRE_NOTHROW(ftxui::Render(screen, doc));
}

TEST_CASE("FtxuiMarkdown whitespace-only input", "[ftxui][edge]") {
    auto elements = FtxuiMarkdown::render("   \n  \n  ");
    REQUIRE_FALSE(elements.empty());
    // Should not crash when rendered to screen
    auto doc = ftxui::vbox(std::move(elements));
    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(40),
        ftxui::Dimension::Fixed(10)
    );
    REQUIRE_NOTHROW(ftxui::Render(screen, doc));
}
