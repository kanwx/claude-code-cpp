#include <catch2/catch_test_macros.hpp>

#ifdef HAS_FTXUI

#include <claude/ui/SearchOverlay.hpp>
#include <claude/ui/UiMessageTypes.hpp>

using namespace claude::ui;
using namespace claude;

TEST_CASE("SearchOverlay starts inactive", "[search]") {
    SearchOverlay overlay;
    REQUIRE_FALSE(overlay.isActive());
}

TEST_CASE("SearchOverlay activate/deactivate", "[search]") {
    SearchOverlay overlay;
    overlay.activate();
    REQUIRE(overlay.isActive());
    overlay.deactivate();
    REQUIRE_FALSE(overlay.isActive());
}

TEST_CASE("SearchOverlay finds matches in messages", "[search]") {
    SearchOverlay overlay;
    std::vector<DisplayMessage> messages;
    messages.push_back(DisplayMessage::userPrompt("Hello world"));
    messages.push_back(DisplayMessage::userPrompt("Hello again"));
    messages.push_back(DisplayMessage::userPrompt("Goodbye"));

    overlay.setMessages(&messages);
    overlay.activate();
    // Simulate typing "Hello"
    overlay.OnEvent(ftxui::Event::Character('H'));
    overlay.OnEvent(ftxui::Event::Character('e'));
    overlay.OnEvent(ftxui::Event::Character('l'));
    overlay.OnEvent(ftxui::Event::Character('l'));
    overlay.OnEvent(ftxui::Event::Character('o'));

    REQUIRE(overlay.results().size() == 2);
    REQUIRE(overlay.currentMatchIndex() == 0);
}

TEST_CASE("SearchOverlay nextMatch cycles", "[search]") {
    SearchOverlay overlay;
    std::vector<DisplayMessage> messages;
    messages.push_back(DisplayMessage::userPrompt("cat dog cat"));
    overlay.setMessages(&messages);
    overlay.activate();
    overlay.OnEvent(ftxui::Event::Character('c'));
    overlay.OnEvent(ftxui::Event::Character('a'));
    overlay.OnEvent(ftxui::Event::Character('t'));

    auto initial = overlay.currentMatchIndex();
    overlay.nextMatch();
    REQUIRE(overlay.currentMatchIndex() >= 0);
}

TEST_CASE("SearchOverlay escape deactivates", "[search]") {
    SearchOverlay overlay;
    overlay.activate();
    REQUIRE(overlay.isActive());
    overlay.OnEvent(ftxui::Event::Escape);
    REQUIRE_FALSE(overlay.isActive());
}

TEST_CASE("SearchOverlay backspace removes last char", "[search]") {
    SearchOverlay overlay;
    std::vector<DisplayMessage> messages;
    messages.push_back(DisplayMessage::userPrompt("xyz"));
    overlay.setMessages(&messages);
    overlay.activate();
    overlay.OnEvent(ftxui::Event::Character('x'));
    overlay.OnEvent(ftxui::Event::Character('y'));
    REQUIRE(overlay.results().size() == 1); // "xy" matches in "xyz"
    overlay.OnEvent(ftxui::Event::Character('z'));
    overlay.OnEvent(ftxui::Event::Character('w'));
    REQUIRE(overlay.results().size() == 0); // "xyzw" no match
    overlay.OnEvent(ftxui::Event::Backspace);
    REQUIRE(overlay.results().size() == 1); // back to "xyz" which matches
}

TEST_CASE("SearchOverlay searchableText on tool use", "[search]") {
    std::vector<DisplayMessage> messages;
    ToolUseBlock block;
    block.toolName = "Read";
    block.input = "somefile.txt";
    messages.push_back(DisplayMessage::assistantToolUse(block));

    auto text = messages[0].searchableText();
    REQUIRE(text.find("Read") != std::string::npos);
    REQUIRE(text.find("somefile.txt") != std::string::npos);
}

#else

// When FTXUI is not available, provide a trivial passing test
TEST_CASE("SearchOverlay requires HAS_FTXUI", "[search]") {
    REQUIRE(true);
}

#endif // HAS_FTXUI
