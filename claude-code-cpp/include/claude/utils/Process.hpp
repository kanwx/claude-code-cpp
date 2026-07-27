#pragma once

#include "../core/Types.hpp"

#include <string>
#include <filesystem>
#include <optional>
#include <atomic>
#include <memory>

namespace claude {

/// 进程执行工具
class Process {
public:
    struct Result {
        int exitCode = 0;
        String stdout;
        String stderr;
        bool timedOut = false;
    };

    /// Per-tool cancel token — Process checks this in the polling loop.
    /// Once set to true, Process kills the process group and returns.
    /// Shared pointer ensures the token outlives the Process call even if
    /// the executor is destroyed mid-flight.
    using CancelToken = std::shared_ptr<const std::atomic<bool>>;

    /// 执行命令
    static Result execute(
        const String& command,
        const std::filesystem::path& workDir = {},
        int timeoutSeconds = 120,
        CancelToken cancelToken = nullptr
    );

    /// Streaming output callback — receives chunks of stdout as they arrive.
    /// Return false to cancel (kill the process).
    using OutputCallback = std::function<bool(const String& chunk)>;

    /// Execute with streaming stdout output.
    /// Calls onOutput for each chunk of stdout as it arrives.
    /// If onOutput returns false, the process is killed.
    static Result executeStreaming(
        const String& command,
        const std::filesystem::path& workDir = {},
        int timeoutSeconds = 120,
        OutputCallback onOutput = nullptr,
        CancelToken cancelToken = nullptr
    );

    /// 检查命令是否存在
    static bool commandExists(const String& command);

    /// 获取 shell
    static String getShell();

    /// Shell-quote a string (single-quote escaping)
    static String shellQuote(const String& arg);
};

} // namespace claude
