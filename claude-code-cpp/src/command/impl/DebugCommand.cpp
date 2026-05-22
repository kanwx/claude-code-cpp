#include <claude/command/impl/DebugCommand.hpp>
#include <claude/command/CommandContext.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/config/SettingsManager.hpp>
#include <claude/api/ApiDebugTracker.hpp>
#include <sstream>
#include <cstdlib>
#include <chrono>

// Platform-specific stack trace support
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <cxxabi.h>
#define HAS_BACKTRACE 1
#endif

// Platform-specific memory support
#if defined(__APPLE__)
#include <mach/task.h>
#include <mach/mach_init.h>
#endif

namespace claude {

namespace {
    // Profiling state
    struct ProfileState {
        bool active = false;
        std::chrono::steady_clock::time_point startTime;
        int apiCalls = 0;
        int fileOps = 0;
        size_t memoryBefore = 0;

        static ProfileState& instance() {
            static ProfileState state;
            return state;
        }
    };

    String getStackTrace(int skipFrames = 2, int maxFrames = 20) {
        std::ostringstream oss;

#if HAS_BACKTRACE
        void* buffer[64];
        int size = backtrace(buffer, maxFrames + skipFrames);
        char** symbols = backtrace_symbols(buffer, size);

        if (symbols == nullptr) {
            return "Unable to get stack trace\n";
        }

        oss << "Stack Trace (" << (size - skipFrames) << " frames):\n";

        for (int i = skipFrames; i < size; ++i) {
            String symbol = symbols[i];

            // Try to demangle C++ names
            // Format: ./executable(function+offset) [address]
            size_t parenStart = symbol.find('(');
            size_t parenEnd = symbol.find('+', parenStart);

            if (parenStart != String::npos && parenEnd != String::npos) {
                String mangled = symbol.substr(parenStart + 1, parenEnd - parenStart - 1);

                if (!mangled.empty() && mangled[0] != '?') {
                    int status = 0;
                    char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);

                    if (status == 0 && demangled) {
                        oss << "  #" << (i - skipFrames) << " " << demangled << "\n";
                        free(demangled);
                    } else {
                        oss << "  #" << (i - skipFrames) << " " << mangled << "\n";
                    }
                } else {
                    oss << "  #" << (i - skipFrames) << " " << symbol << "\n";
                }
            } else {
                oss << "  #" << (i - skipFrames) << " " << symbol << "\n";
            }
        }

        free(symbols);
#else
        oss << "Stack trace not available on this platform.\n";
        oss << "Supported: macOS, Linux\n";
#endif

        return oss.str();
    }

    size_t getCurrentMemoryUsage() {
#if defined(__APPLE__)
        // macOS: use task_info
        task_t task = mach_task_self();
        struct task_basic_info info;
        mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;

        if (task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
            return info.resident_size;
        }
#elif defined(__linux__)
        // Linux: read /proc/self/status
        FILE* f = fopen("/proc/self/status", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "VmRSS:", 6) == 0) {
                    fclose(f);
                    return strtoull(line + 6, nullptr, 10) * 1024;
                }
            }
            fclose(f);
        }
#endif
        return 0;
    }
}

String DebugCommand::execute(const String& args, CommandContext& context) {
    std::istringstream iss(args);
    String action;
    iss >> action;

    std::ostringstream oss;
    oss << "=== Debug Mode ===\n\n";

    bool debugMode = SettingsManager::instance().getDebugMode();

    if (action.empty()) {
        // Toggle
        if (debugMode) {
            SettingsManager::instance().setDebugMode(false);
            oss << "Status: OFF (was ON)\n";
        } else {
            SettingsManager::instance().setDebugMode(true);
            oss << "Status: ON (was OFF)\n";
        }
        oss << "\nUsage: /debug [on|off|log|trace|profile|dump]\n";

    } else if (action == "on" || action == "enable") {
        SettingsManager::instance().setDebugMode(true);
        ApiDebugTracker::instance().setEnabled(true);
        oss << "Debug mode enabled.\n\n";
        oss << "Features:\n";
        oss << "  - Verbose logging\n";
        oss << "  - Stack traces on error\n";
        oss << "  - API request/response tracking\n";

    } else if (action == "off" || action == "disable") {
        SettingsManager::instance().setDebugMode(false);
        ApiDebugTracker::instance().setEnabled(false);
        oss << "Debug mode disabled.\n";

    } else if (action == "log") {
        String level;
        iss >> level;

        if (level.empty()) {
            const char* currentLevel = std::getenv("SPDLOG_LEVEL");
            oss << "Current log level: " << (currentLevel ? currentLevel : "info") << "\n\n";
            oss << "Levels: trace, debug, info, warn, error, critical\n";
        } else {
            setenv("SPDLOG_LEVEL", level.c_str(), 1);
            oss << "Log level set to: " << level << "\n";
        }

    } else if (action == "trace") {
        oss << getStackTrace(2, 20);
        oss << "\nTip: Build with debug symbols (-g) for better traces.\n";

    } else if (action == "profile") {
        String subAction;
        iss >> subAction;

        auto& profile = ProfileState::instance();

        if (subAction == "start") {
            profile.active = true;
            profile.startTime = std::chrono::steady_clock::now();
            profile.apiCalls = 0;
            profile.fileOps = 0;
            profile.memoryBefore = getCurrentMemoryUsage();
            oss << "Profiling started.\n";
            oss << "Memory baseline: " << (profile.memoryBefore / 1024) << " KB\n";
            oss << "\nStop with: /debug profile stop\n";

        } else if (subAction == "stop") {
            if (!profile.active) {
                oss << "Profiling was not running.\n";
            } else {
                auto endTime = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    endTime - profile.startTime).count();
                size_t memoryAfter = getCurrentMemoryUsage();

                oss << "Profiling stopped.\n\n";
                oss << "Results:\n";
                oss << "  Duration: " << duration << " ms\n";
                oss << "  Memory delta: " << ((int64_t)memoryAfter - (int64_t)profile.memoryBefore) / 1024 << " KB\n";
                oss << "  Memory current: " << (memoryAfter / 1024) << " KB\n";

                profile.active = false;
            }

        } else if (subAction == "status") {
            if (profile.active) {
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - profile.startTime).count();
                oss << "Profiling: ACTIVE (" << duration << " ms)\n";
            } else {
                oss << "Profiling: inactive\n";
            }

        } else {
            oss << "Usage:\n";
            oss << "  /debug profile start  - Start profiling\n";
            oss << "  /debug profile stop   - Stop and show results\n";
            oss << "  /debug profile status - Show current status\n";
        }

    } else if (action == "dump") {
        String target;
        iss >> target;

        if (target == "api") {
            // Show recent API call history
            auto& tracker = ApiDebugTracker::instance();
            auto stats = tracker.getStats();
            auto recent = tracker.getRecent(10);

            oss << "=== API Call History ===\n\n";
            oss << "Stats:\n";
            oss << "  Total calls: " << stats.value("totalCalls", 0) << "\n";
            oss << "  Success: " << stats.value("successCalls", 0) << "\n";
            oss << "  Failed: " << stats.value("failedCalls", 0) << "\n";
            oss << "  Avg duration: " << stats.value("avgDurationMs", 0.0) << " ms\n";
            oss << "  Total input tokens: " << stats.value("totalInputTokens", 0) << "\n";
            oss << "  Total output tokens: " << stats.value("totalOutputTokens", 0) << "\n\n";

            if (recent.empty()) {
                oss << "No API calls recorded.\n";
                oss << "Enable tracking: /debug on\n";
            } else {
                oss << "Recent calls:\n";
                for (const auto& e : recent) {
                    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(e.endTime - e.startTime).count();
                    oss << "  [" << e.method << "] " << e.provider << "/" << e.model
                        << " status=" << (e.success ? "OK" : "FAIL")
                        << " http=" << e.httpStatus
                        << " in=" << e.inputTokens << " out=" << e.outputTokens
                        << " dur=" << dur << "ms";
                    if (!e.error.empty()) oss << " err=" << e.error.substr(0, 80);
                    oss << "\n";
                }
            }

        } else {
            oss << "Dump: " << (target.empty() ? "all" : target) << "\n\n";

            // Context info
            oss << "Context:\n";
            if (context.agentLoop) {
                oss << "  AgentLoop: active\n";
            }
            if (context.tools) {
                oss << "  Tools: " << context.tools->size() << "\n";
            }
            if (context.permissionEngine) {
                oss << "  Permissions: active\n";
            }

            // Memory info
            size_t memory = getCurrentMemoryUsage();
            if (memory > 0) {
                oss << "  Memory: " << (memory / 1024) << " KB\n";
            }

            // Environment
            oss << "\nEnvironment:\n";
            oss << "  Debug: " << (debugMode ? "ON" : "OFF") << "\n";
            const char* logLevel = std::getenv("SPDLOG_LEVEL");
            oss << "  Log level: " << (logLevel ? logLevel : "default") << "\n";
        }

    } else if (action == "memory") {
        size_t memory = getCurrentMemoryUsage();
        oss << "Memory Usage:\n";
        oss << "  Current: " << (memory / 1024) << " KB (" << (memory / 1024 / 1024) << " MB)\n";

    } else {
        oss << "Unknown action: " << action << "\n";
        oss << "Actions: on, off, log, trace, profile, dump, memory\n";
    }

    return oss.str();
}

} // namespace claude
