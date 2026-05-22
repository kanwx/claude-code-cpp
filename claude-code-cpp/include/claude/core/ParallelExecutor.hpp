#pragma once

#include "../core/Types.hpp"
#include <functional>
#include <vector>
#include <future>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <nlohmann/json.hpp>

namespace claude {

using Json = nlohmann::json;

/// 并行执行结果
template<typename T>
struct ParallelResult {
    size_t index;
    T value;
    bool success;
    String error;
};

/// 并行执行器
class ParallelExecutor {
public:
    explicit ParallelExecutor(size_t maxThreads = std::thread::hardware_concurrency());
    ~ParallelExecutor();

    /// 并行执行多个任务
    template<typename F, typename... Args>
    auto executeParallel(std::vector<std::pair<String, F>> tasks, Args&&... args)
        -> std::vector<ParallelResult<decltype(std::declval<F>()(std::forward<Args>(args)...))>>;

    /// 并行执行（索引版本）
    template<typename F>
    auto executeIndexed(size_t count, F&& func)
        -> std::vector<ParallelResult<decltype(std::declval<F>()(0))>>;

    /// 获取活动线程数
    size_t activeCount() const;

    /// 等待所有任务完成
    void waitAll();

    /// 设置最大线程数
    void setMaxThreads(size_t n) { maxThreads_ = n; }

    /// Enqueue a task and return a future for the result.
    template<typename F>
    auto enqueue(F&& func) -> std::future<decltype(func())> {
        using ReturnType = decltype(func());
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push([promise, f = std::forward<F>(func)]() mutable {
                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        f();
                        promise->set_value();
                    } else {
                        promise->set_value(f());
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });
        }
        cv_.notify_one();
        return future;
    }

private:
    size_t maxThreads_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable done_;
    size_t active_ = 0;
    bool stop_ = false;
};

// ========== 实现 ==========

template<typename F, typename... Args>
auto ParallelExecutor::executeParallel(std::vector<std::pair<String, F>> tasks, Args&&... args)
    -> std::vector<ParallelResult<decltype(std::declval<F>()(std::forward<Args>(args)...))>>
{
    using ResultType = decltype(std::declval<F>()(std::forward<Args>(args)...));
    std::vector<ParallelResult<ResultType>> results(tasks.size());

    std::vector<std::future<void>> futures;
    futures.reserve(tasks.size());

    for (size_t i = 0; i < tasks.size(); ++i) {
        auto& [name, func] = tasks[i];

        std::promise<void> promise;
        futures.push_back(promise.get_future());

        std::thread([&, i, promise = std::move(promise)]() mutable {
            try {
                auto result = func(std::forward<Args>(args)...);
                results[i] = {i, std::move(result), true, ""};
            } catch (const std::exception& e) {
                results[i] = {i, ResultType{}, false, e.what()};
            } catch (...) {
                results[i] = {i, ResultType{}, false, "Unknown error"};
            }
            promise.set_value();
        }).detach();
    }

    // 等待所有完成
    for (auto& f : futures) {
        f.wait();
    }

    return results;
}

template<typename F>
auto ParallelExecutor::executeIndexed(size_t count, F&& func)
    -> std::vector<ParallelResult<decltype(std::declval<F>()(0))>>
{
    using ResultType = decltype(std::declval<F>()(0));
    std::vector<ParallelResult<ResultType>> results(count);

    if (count == 0) return results;

    // 确定实际线程数
    size_t numThreads = std::min(count, maxThreads_);

    std::atomic<size_t> nextIndex{0};
    std::atomic<size_t> completed{0};

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, this]() {
            while (true) {
                size_t i = nextIndex.fetch_add(1);
                if (i >= count) break;

                try {
                    auto result = func(i);
                    results[i] = {i, std::move(result), true, ""};
                } catch (const std::exception& e) {
                    results[i] = {i, ResultType{}, false, e.what()};
                } catch (...) {
                    results[i] = {i, ResultType{}, false, "Unknown error"};
                }

                completed++;
            }
        });
    }

    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }

    return results;
}

/// 工具并行执行助手
class ParallelToolExecutor {
public:
    /// Tool executor callback: takes (toolName, input) → result JSON
    using ToolExecutorFn = std::function<Json(const String&, const Json&)>;

    /// 检查工具是否可以并行执行
    static bool canRunInParallel(const String& tool1, const String& tool2);

    /// 执行多个只读工具 with real tool execution
    static std::vector<Json> executeReadTools(
        const std::vector<std::pair<String, Json>>& tools,
        ToolExecutorFn executor,
        size_t maxThreads = 4
    );
};

} // namespace claude
