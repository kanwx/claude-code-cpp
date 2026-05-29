#include <claude/console/BannerPrinter.hpp>
#include <claude/console/AnsiStyle.hpp>

namespace claude {

BannerPrinter::BannerPrinter(std::ostream& out) : out_(out) {}

void BannerPrinter::print() {
    // Intentionally empty — banner removed to match official Claude Code UX
}

void BannerPrinter::printVersion(const String& version) {
    // Intentionally empty — banner removed to match official Claude Code UX
}

void BannerPrinter::printWelcome() {
    // Intentionally empty — banner removed to match official Claude Code UX
}

} // namespace claude
