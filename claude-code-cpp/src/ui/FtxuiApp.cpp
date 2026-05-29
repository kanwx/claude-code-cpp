#ifdef HAS_FTXUI

#include "claude/ui/FtxuiApp.hpp"
#include <spdlog/spdlog.h>

namespace claude {

FtxuiApp::FtxuiApp() {
    spdlog::debug("FTXUI app initialized");
}

FtxuiApp::~FtxuiApp() {
    running_ = false;
}

void FtxuiApp::addAssistantMessage(const String& content) {
    messages_.push_back({Message::Assistant, content, "", ""});
}

void FtxuiApp::addUserMessage(const String& content) {
    messages_.push_back({Message::User, content, "", ""});
}

void FtxuiApp::addToolMessage(const String& toolName, const String& input, const String& result) {
    messages_.push_back({Message::Tool, input, toolName, result});
}

void FtxuiApp::setStatus(const String& status, int elapsedSeconds, int tokens) {
    currentStatus_ = status;
    elapsedSeconds_ = elapsedSeconds;
    currentTokens_ = tokens;
}

void FtxuiApp::clearConversation() {
    messages_.clear();
}

ftxui::Component FtxuiApp::buildInputComponent() {
    using namespace ftxui;

    class InputComponent : public ComponentBase {
    public:
        InputComponent(FtxuiApp* app) : app_(app) {}

        Element OnRender() override {
            return hbox({
                text("❯ ") | color(Color::Green) | bold,
                text(input_) | color(Color::White),
                text("▏") | color(Color::GrayLight),
            });
        }

        bool OnEvent(Event event) override {
            if (event == Event::Return) {
                if (!input_.empty()) {
                    if (input_[0] == '/' && app_->onCommand_) {
                        app_->onCommand_(input_);
                    } else if (app_->onSubmit_) {
                        app_->onSubmit_(input_);
                    }
                    app_->addUserMessage(input_);
                    input_.clear();
                }
                return true;
            }
            if (event.is_character()) {
                input_ += event.character();
                return true;
            }
            if (event == Event::Backspace && !input_.empty()) {
                input_.pop_back();
                return true;
            }
            // Handle Ctrl+C
            if (event == Event::Escape) {
                app_->exit();
                return true;
            }
            return false;
        }

        bool Focusable() const override { return true; }

    private:
        String input_;
        FtxuiApp* app_;
    };

    return std::make_shared<InputComponent>(this);
}

ftxui::Component FtxuiApp::buildMessagesComponent() {
    using namespace ftxui;

    class MessagesComponent : public ComponentBase {
    public:
        MessagesComponent(FtxuiApp* app) : app_(app) {}

        Element OnRender() override {
            std::vector<Element> elements;

            for (const auto& msg : app_->messages_) {
                switch (msg.type) {
                    case Message::User:
                        elements.push_back(FormatMessage(msg.content, true));
                        break;
                    case Message::Assistant:
                        elements.push_back(FormatMessage(msg.content, false));
                        break;
                    case Message::Tool:
                        elements.push_back(FormatToolMessage(msg.toolName, msg.toolResult));
                        break;
                }
            }

            if (elements.empty()) {
                return text("Welcome to Claude Code C++! Type your message and press Enter.")
                    | color(Color::GrayLight);
            }

            return vbox(std::move(elements));
        }

    private:
        FtxuiApp* app_;
    };

    return std::make_shared<MessagesComponent>(this);
}

ftxui::Component FtxuiApp::buildStatusComponent() {
    using namespace ftxui;

    class StatusComponent : public ComponentBase {
    public:
        StatusComponent(FtxuiApp* app) : app_(app) {}

        Element OnRender() override {
            if (app_->currentStatus_.empty()) {
                return text("");
            }
            return FormatStatus(app_->currentStatus_, app_->elapsedSeconds_, app_->currentTokens_);
        }

    private:
        FtxuiApp* app_;
    };

    return std::make_shared<StatusComponent>(this);
}

ftxui::Component FtxuiApp::buildMainComponent() {
    using namespace ftxui;

    auto input = buildInputComponent();
    auto messages = buildMessagesComponent();
    auto status = buildStatusComponent();

    // Layout: messages (flexible) | separator | status | input
    return Container::Vertical({
        messages,
        status,
        input,
    });
}

void FtxuiApp::run() {
    using namespace ftxui;

    auto component = buildMainComponent();

    // Main loop
    auto screen = ScreenInteractive::Fullscreen();
    screen.Loop(component);
}

void FtxuiApp::exit() {
    running_ = false;
    screen_.Exit();
}

} // namespace claude

#endif // HAS_FTXUI
