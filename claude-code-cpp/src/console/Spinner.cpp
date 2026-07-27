#include <claude/console/Spinner.hpp>
#include <claude/console/AnsiStyle.hpp>
#include <claude/console/CreativeVerbs.hpp>
#include <claude/console/ActivityDescription.hpp>
#include <sstream>
#include <iomanip>
#include <unistd.h>

namespace claude {

constexpr const char* Spinner::FRAMES[];

Spinner::Spinner() : out_(std::cerr) {
    currentVerb_ = console::CreativeVerbs::randomCreativeVerb();
}

Spinner::Spinner(std::ostream& out) : out_(out) {
    currentVerb_ = console::CreativeVerbs::randomCreativeVerb();
}

Spinner::~Spinner() {
    stop();
}

void Spinner::updateVerb() {
    verbFrameCounter_++;
    if (verbFrameCounter_ >= VERB_CHANGE_INTERVAL) {
        verbFrameCounter_ = 0;
        verbIndex_ = (verbIndex_ + 1) % console::CreativeVerbs::creativeVerbCount();
        currentVerb_ = console::CreativeVerbs::getCreativeVerb(verbIndex_);
    }
}

void Spinner::start(const String& message) {
    if (running_) return;

    // No-op when stderr is not a terminal — prevents ANSI escape
    // sequences from accumulating as garbage when stderr is piped
    // or redirected (equivalent guard to StatusLine::render).
    if (!isatty(STDERR_FILENO)) return;

    message_ = message;
    if (message_.empty()) {
        currentVerb_ = console::CreativeVerbs::randomCreativeVerb();
    }
    running_ = true;
    frameIndex_ = 0;
    verbFrameCounter_ = 0;
    startTime_ = std::chrono::steady_clock::now();

    out_ << AnsiStyle::HIDE_CURSOR << std::flush;
    thread_ = std::thread(&Spinner::animate, this);
}

void Spinner::stop() {
    if (!running_) return;

    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }

    out_ << "\r" << AnsiStyle::CLEAR_LINE << AnsiStyle::SHOW_CURSOR << std::flush;
}

void Spinner::setMessage(const String& message) {
    message_ = message;
}

void Spinner::setToolContext(const String& toolName, const String& toolArgs) {
    toolContextName_ = toolName;
    toolContextArgs_ = toolArgs;
    hasToolContext_ = true;
}

void Spinner::clearToolContext() {
    hasToolContext_ = false;
    toolContextName_.clear();
    toolContextArgs_.clear();
}

void Spinner::setThinking(bool thinking) {
    isThinking_ = thinking;
}

String Spinner::formatTokenCount(int tokens) {
    if (tokens < 0) return "";
    if (tokens >= 1'000'000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (tokens / 1'000'000.0) << "M";
        return oss.str();
    }
    if (tokens >= 10'000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (tokens / 1'000.0) << "K";
        return oss.str();
    }
    return std::to_string(tokens);
}

String Spinner::formatElapsedTime(std::chrono::steady_clock::time_point start) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

    std::ostringstream oss;
    if (elapsed >= 60) {
        int mins = elapsed / 60;
        int secs = elapsed % 60;
        oss << mins << "m " << secs << "s";
    } else {
        oss << elapsed << "s";
    }
    return oss.str();
}

String Spinner::buildDisplayString() {
    std::ostringstream oss;

    // Spinner frame
    oss << AnsiStyle::BRIGHT_CYAN << FRAMES[frameIndex_ % FRAME_COUNT] << AnsiStyle::RESET;

    // Context-aware display
    if (hasToolContext_ && !toolContextName_.empty()) {
        // Tool-specific context: ⠹ Reading src/main.ts (3s · 80 tokens)
        oss << " ";
        String activity = getActivityDescription(toolContextName_, toolContextArgs_, true);
        oss << AnsiStyle::toolFgColor(toolContextName_) << activity << AnsiStyle::RESET;
    } else if (isThinking_) {
        // Thinking mode
        oss << " " << AnsiStyle::DIM << "\xe2\x9c\xa7" << " Thinking" << AnsiStyle::RESET;
        // ✧ (U+2727) or use 💭 emoji
    } else if (!message_.empty()) {
        oss << " " << message_;
    } else {
        // Creative verb
        oss << " " << currentVerb_;
    }

    // Elapsed time + tokens
    oss << AnsiStyle::DIM << " (" << formatElapsedTime(startTime_);

    if (tokenProvider_) {
        int tokens = tokenProvider_();
        if (tokens > 0) {
            oss << " \xc2\xb7 " << formatTokenCount(tokens) << " tokens";  // · middle dot
        }
    }

    oss << ")" << AnsiStyle::RESET;
    return oss.str();
}

void Spinner::animate() {
    if (!isatty(STDERR_FILENO)) return;

    while (running_) {
        updateVerb();

        out_ << "\r" << AnsiStyle::CLEAR_LINE;
        out_ << buildDisplayString();
        out_ << std::flush;

        frameIndex_++;
        std::this_thread::sleep_for(FRAME_DELAY);
    }
}

} // namespace claude
