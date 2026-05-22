#include <claude/core/ParallelExecutor.hpp>
#include <algorithm>

namespace claude {

ParallelExecutor::ParallelExecutor(size_t maxThreads)
    : maxThreads_(maxThreads > 0 ? maxThreads : 1)
{}

ParallelExecutor::~ParallelExecutor() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

size_t ParallelExecutor::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

void ParallelExecutor::waitAll() {
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return active_ == 0 && tasks_.empty(); });
}

// ========== ParallelToolExecutor ==========

bool ParallelToolExecutor::canRunInParallel(const String& tool1, const String& tool2) {
    // 只读工具可以并行
    static const std::vector<String> readOnlyTools = {
        "Read", "Glob", "Grep", "WebFetch", "WebSearch",
        "TaskList", "TaskGet", "LSP"
    };

    auto isReadOnly = [](const String& tool) {
        for (const auto& t : readOnlyTools) {
            if (tool == t || tool.find(t) != String::npos) {
                return true;
            }
        }
        return false;
    };

    // 两个都是只读工具才能并行
    return isReadOnly(tool1) && isReadOnly(tool2);
}

std::vector<Json> ParallelToolExecutor::executeReadTools(
    const std::vector<std::pair<String, Json>>& tools,
    ToolExecutorFn executor,
    size_t maxThreads
) {
    std::vector<Json> results(tools.size());

    if (tools.empty()) return results;

    // 验证所有工具都是只读的
    for (const auto& [name, _] : tools) {
        if (!canRunInParallel(name, name)) {
            for (size_t i = 0; i < results.size(); ++i) {
                results[i] = {{"error", "Non-read tool in parallel execution"}};
            }
            return results;
        }
    }

    // 并行执行 with real tool executor
    ParallelExecutor pe(maxThreads);

    auto resultsWithMeta = pe.executeIndexed(tools.size(), [&](size_t i) -> Json {
        const auto& [name, input] = tools[i];
        return executor(name, input);
    });

    for (size_t i = 0; i < resultsWithMeta.size(); ++i) {
        if (resultsWithMeta[i].success) {
            results[i] = resultsWithMeta[i].value;
        } else {
            results[i] = {{"error", resultsWithMeta[i].error}};
        }
    }

    return results;
}

} // namespace claude
