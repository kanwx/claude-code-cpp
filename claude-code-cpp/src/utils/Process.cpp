#include <claude/utils/Process.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <cerrno>
#endif

namespace claude {

#ifndef _WIN32
/// Terminate a process group: SIGTERM → grace period → SIGKILL.
/// Reused by both timeout and external cancellation paths.
void terminateProcessGroup(pid_t pgid, const char* reason) {
    spdlog::warn("Process: {} — sending SIGTERM to pgid {}", reason, pgid);
    kill(-pgid, SIGTERM);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // Check if still alive, then force-kill
    if (kill(-pgid, 0) == 0) {
        spdlog::warn("Process: SIGTERM not enough — sending SIGKILL to pgid {}", pgid);
        kill(-pgid, SIGKILL);
    }
}
#endif

String Process::shellQuote(const String& arg) {
    String quoted = "'";
    for (char c : arg) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

Process::Result Process::execute(
    const String& command,
    const std::filesystem::path& workDir,
    int timeoutSeconds,
    CancelToken cancelToken
) {
    Result result;

    String shell = getShell();

    // On macOS, replace 'grep' with absolute path to avoid shell function overrides.
    String cmd = command;
#ifdef __APPLE__
    {
        static String grepPath;
        static bool grepPathChecked = false;
        if (!grepPathChecked) {
            grepPathChecked = true;
            if (std::filesystem::exists("/usr/local/bin/ggrep")) {
                grepPath = "/usr/local/bin/ggrep";
            } else {
                grepPath = "/usr/bin/grep";
            }
        }
        if (!grepPath.empty()) {
            size_t pos = 0;
            while ((pos = cmd.find("grep", pos)) != String::npos) {
                bool prevOk = (pos == 0 || cmd[pos-1] == ' ' || cmd[pos-1] == '|' ||
                               cmd[pos-1] == ';' || cmd[pos-1] == '&' || cmd[pos-1] == '(');
                bool nextOk = (pos + 4 >= cmd.size() || cmd[pos+4] == ' ' || cmd[pos+4] == '\t' ||
                               cmd[pos+4] == '\n' || cmd[pos+4] == '|' || cmd[pos+4] == ';' ||
                               cmd[pos+4] == '&' || cmd[pos+4] == ')');
                bool isEfgrep = (pos > 0 && (cmd[pos-1] == 'e' || cmd[pos-1] == 'f'));
                bool alreadyAbs = (pos >= 5 && cmd.compare(pos-5, 5, "/bin/") == 0) ||
                                  (pos >= 9 && cmd.compare(pos-9, 9, "/usr/bin/") == 0) ||
                                  (pos >= 14 && cmd.compare(pos-14, 14, "/usr/local/bin/") == 0);

                if (prevOk && nextOk && !isEfgrep && !alreadyAbs) {
                    cmd.replace(pos, 4, grepPath);
                    pos += grepPath.size();
                } else {
                    pos += 4;
                }
            }
        }
    }
#endif

    // Capture stderr via temp file
    const char* tmpdir = std::getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";

    char tmpPath[256];
    snprintf(tmpPath, sizeof(tmpPath), "%s/claude_stderr_XXXXXX", tmpdir);
    int tmpFd = mkstemp(tmpPath);
    if (tmpFd == -1) {
        // Fallback without stderr capture
        String fullCmd = shell + " -c " + shellQuote(cmd);
        FILE* pipe = popen(fullCmd.c_str(), "r");
        if (!pipe) {
            result.exitCode = -1;
            result.stderr = "Failed to execute command";
            return result;
        }
        std::array<char, 4096> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe)) {
            result.stdout += buffer.data();
        }
        int rawStatus = pclose(pipe);
        if (WIFEXITED(rawStatus)) result.exitCode = WEXITSTATUS(rawStatus);
        else result.exitCode = rawStatus;
        return result;
    }
    close(tmpFd);

    // Build the inner shell command that redirects stderr
    // Wrap in { ... ; } so the 2> redirect applies to the entire command,
    // not just the last statement in a compound command like "a; b 2>file".
    String innerCmd = "{ " + cmd + " ; } 2>" + tmpPath;
    if (!workDir.empty()) {
        innerCmd = "cd " + shellQuote(workDir.string()) + " && " + innerCmd;
    }

    String fullCmd = shell + " -c " + shellQuote(innerCmd);

    spdlog::debug("Process::execute: {} (timeout={}s)", fullCmd, timeoutSeconds);

#ifndef _WIN32
    // Use fork + exec + timeout on Unix
    // Create pipes for stdout capture
    int stdoutPipe[2];
    if (pipe(stdoutPipe) == -1) {
        unlink(tmpPath);
        result.exitCode = -1;
        result.stderr = "Failed to create pipe";
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        unlink(tmpPath);
        result.exitCode = -1;
        result.stderr = "Failed to fork";
        return result;
    }

    if (pid == 0) {
        // Child process
        close(stdoutPipe[0]);  // Close read end
        dup2(stdoutPipe[1], STDOUT_FILENO);  // Redirect stdout to pipe
        close(stdoutPipe[1]);

        // Create new process group so we can kill the whole group on timeout
        setpgid(0, 0);

        execl(shell.c_str(), shell.c_str(), "-c", innerCmd.c_str(), nullptr);
        // If exec fails
        _exit(127);
    }

    // Parent process
    close(stdoutPipe[1]);  // Close write end

    // Read stdout with timeout
    std::atomic<bool> timedOut{false};
    std::atomic<bool> readingDone{false};
    String capturedStdout;

    std::thread readerThread([&]() {
        std::array<char, 4096> buffer;
        ssize_t bytesRead;
        while ((bytesRead = read(stdoutPipe[0], buffer.data(), buffer.size())) > 0) {
            capturedStdout.append(buffer.data(), bytesRead);
        }
        close(stdoutPipe[0]);
        readingDone = true;
    });

    // Wait for child with timeout
    auto startTime = std::chrono::steady_clock::now();
    bool childExited = false;
    int rawStatus = 0;
    bool externallyCancelled = false;

    while (!childExited) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();

        // External cancellation check (per-tool cancel token from executor)
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            externallyCancelled = true;
            terminateProcessGroup(pid, "externally cancelled");
            waitpid(pid, &rawStatus, 0);
            break;
        }

        if (timeoutSeconds > 0 && elapsed >= timeoutSeconds) {
            // Timeout: kill the entire process group
            timedOut = true;
            terminateProcessGroup(pid, "timeout");
            waitpid(pid, &rawStatus, 0);
            break;
        }

        // Check if child exited (non-blocking waitpid)
        int waitStatus;
        pid_t waited = waitpid(pid, &waitStatus, WNOHANG);
        if (waited == pid) {
            rawStatus = waitStatus;
            childExited = true;
            break;
        } else if (waited == -1 && errno != EINTR) {
            // waitpid error
            rawStatus = 0;
            childExited = true;
            break;
        }

        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Wait for reader thread
    if (readerThread.joinable()) {
        readerThread.join();
    }

    result.stdout = capturedStdout;

    if (externallyCancelled || timedOut) {
        if (timedOut) result.timedOut = true;
        result.exitCode = -1;
    } else if (WIFEXITED(rawStatus)) {
        result.exitCode = WEXITSTATUS(rawStatus);
    } else if (WIFSIGNALED(rawStatus)) {
        result.exitCode = -WTERMSIG(rawStatus);
    } else {
        result.exitCode = rawStatus;
    }

#else
    // Windows fallback (no timeout support in popen mode)
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        unlink(tmpPath);
        result.exitCode = -1;
        result.stderr = "Failed to execute command";
        return result;
    }

    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result.stdout += buffer.data();
    }

    int rawStatus = pclose(pipe);
    if (WIFEXITED(rawStatus)) {
        result.exitCode = WEXITSTATUS(rawStatus);
    } else if (WIFSIGNALED(rawStatus)) {
        result.exitCode = -WTERMSIG(rawStatus);
    } else {
        result.exitCode = rawStatus;
    }
#endif

    // Read captured stderr
    {
        std::ifstream errFile(tmpPath);
        if (errFile) {
            std::ostringstream ss;
            ss << errFile.rdbuf();
            result.stderr = ss.str();
        }
    }
    unlink(tmpPath);

    return result;
}

Process::Result Process::executeStreaming(
    const String& command,
    const std::filesystem::path& workDir,
    int timeoutSeconds,
    OutputCallback onOutput,
    CancelToken cancelToken
) {
    Result result;

    if (!onOutput && !cancelToken) {
        return execute(command, workDir, timeoutSeconds);
    }

    String shell = getShell();

    // Same grep path fix as execute()
    String cmd = command;
#ifdef __APPLE__
    {
        static String grepPath;
        static bool grepPathChecked = false;
        if (!grepPathChecked) {
            grepPathChecked = true;
            if (std::filesystem::exists("/usr/local/bin/ggrep")) {
                grepPath = "/usr/local/bin/ggrep";
            } else {
                grepPath = "/usr/bin/grep";
            }
        }
        if (!grepPath.empty()) {
            size_t pos = 0;
            while ((pos = cmd.find("grep", pos)) != String::npos) {
                bool prevOk = (pos == 0 || cmd[pos-1] == ' ' || cmd[pos-1] == '|' ||
                               cmd[pos-1] == ';' || cmd[pos-1] == '&' || cmd[pos-1] == '(');
                bool nextOk = (pos + 4 >= cmd.size() || cmd[pos+4] == ' ' || cmd[pos+4] == '\t' ||
                               cmd[pos+4] == '\n' || cmd[pos+4] == '|' || cmd[pos+4] == ';' ||
                               cmd[pos+4] == '&' || cmd[pos+4] == ')');
                bool isEfgrep = (pos > 0 && (cmd[pos-1] == 'e' || cmd[pos-1] == 'f'));
                bool alreadyAbs = (pos >= 5 && cmd.compare(pos-5, 5, "/bin/") == 0) ||
                                  (pos >= 9 && cmd.compare(pos-9, 9, "/usr/bin/") == 0) ||
                                  (pos >= 14 && cmd.compare(pos-14, 14, "/usr/local/bin/") == 0);

                if (prevOk && nextOk && !isEfgrep && !alreadyAbs) {
                    cmd.replace(pos, 4, grepPath);
                    pos += grepPath.size();
                } else {
                    pos += 4;
                }
            }
        }
    }
#endif

    const char* tmpdir = std::getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";

    char tmpPath[256];
    snprintf(tmpPath, sizeof(tmpPath), "%s/claude_stderr_XXXXXX", tmpdir);
    int tmpFd = mkstemp(tmpPath);
    if (tmpFd == -1) {
        // Fallback to non-streaming
        return execute(command, workDir, timeoutSeconds);
    }
    close(tmpFd);

    String innerCmd = "{ " + cmd + " ; } 2>" + tmpPath;
    if (!workDir.empty()) {
        innerCmd = "cd " + shellQuote(workDir.string()) + " && " + innerCmd;
    }

#ifndef _WIN32
    int stdoutPipe[2];
    if (pipe(stdoutPipe) == -1) {
        unlink(tmpPath);
        return execute(command, workDir, timeoutSeconds);
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        unlink(tmpPath);
        return execute(command, workDir, timeoutSeconds);
    }

    if (pid == 0) {
        close(stdoutPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdoutPipe[1]);
        setpgid(0, 0);
        execl(shell.c_str(), shell.c_str(), "-c", innerCmd.c_str(), nullptr);
        _exit(127);
    }

    close(stdoutPipe[1]);

    // Streaming reader: deliver chunks via callback
    std::atomic<bool> cancelled{false};
    std::atomic<bool> readingDone{false};
    String capturedStdout;

    std::thread readerThread([&]() {
        std::array<char, 4096> buffer;
        ssize_t bytesRead;
        while ((bytesRead = read(stdoutPipe[0], buffer.data(), buffer.size())) > 0) {
            String chunk(buffer.data(), bytesRead);
            capturedStdout += chunk;
            if (!cancelled) {
                if (!onOutput(chunk)) {
                    cancelled = true;
                }
            }
        }
        close(stdoutPipe[0]);
        readingDone = true;
    });

    // Wait for child with timeout + cancellation
    auto startTime = std::chrono::steady_clock::now();
    bool childExited = false;
    int rawStatus = 0;
    bool externallyCancelled = false;

    while (!childExited) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();

        // External cancellation check (per-tool cancel token)
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            terminateProcessGroup(pid, "externally cancelled (streaming)");
            waitpid(pid, &rawStatus, 0);
            externallyCancelled = true;
            break;
        }

        // Internal cancellation (streaming callback returned false)
        if (cancelled) {
            terminateProcessGroup(pid, "internally cancelled (streaming)");
            waitpid(pid, &rawStatus, 0);
            break;
        }

        if (timeoutSeconds > 0 && elapsed >= timeoutSeconds) {
            spdlog::warn("Process::executeStreaming: timeout after {}s, killing pgid {}", timeoutSeconds, pid);
            terminateProcessGroup(pid, "timeout (streaming)");
            waitpid(pid, &rawStatus, 0);
            result.timedOut = true;
            break;
        }

        int waitStatus;
        pid_t waited = waitpid(pid, &waitStatus, WNOHANG);
        if (waited == pid) {
            rawStatus = waitStatus;
            childExited = true;
            break;
        } else if (waited == -1 && errno != EINTR) {
            rawStatus = 0;
            childExited = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (readerThread.joinable()) {
        readerThread.join();
    }

    result.stdout = capturedStdout;

    if (externallyCancelled || cancelled || result.timedOut) {
        result.exitCode = -1;
    } else if (WIFEXITED(rawStatus)) {
        result.exitCode = WEXITSTATUS(rawStatus);
    } else if (WIFSIGNALED(rawStatus)) {
        result.exitCode = -WTERMSIG(rawStatus);
    } else {
        result.exitCode = rawStatus;
    }
#else
    // Windows fallback: no streaming, use non-streaming execute
    unlink(tmpPath);
    return execute(command, workDir, timeoutSeconds);
#endif

    // Read captured stderr
    {
        std::ifstream errFile(tmpPath);
        if (errFile) {
            std::ostringstream ss;
            ss << errFile.rdbuf();
            result.stderr = ss.str();
        }
    }
    unlink(tmpPath);

    return result;
}

bool Process::commandExists(const String& command) {
#ifdef _WIN32
    String checkCmd = "where " + command;
#else
    String checkCmd = "which " + command;
#endif

    auto result = execute(checkCmd);
    return result.exitCode == 0;
}

String Process::getShell() {
#ifdef _WIN32
    return "cmd";
#else
    const char* shell = std::getenv("SHELL");
    if (shell) return shell;
    return "/bin/bash";
#endif
}

} // namespace claude
