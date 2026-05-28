#pragma once

#include "Core.hpp"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <future>
#include <functional>
#include <chrono>

namespace ontology {

// ============================================================================
// 连接池基类
// ============================================================================

template<typename Connection>
class ConnectionPool {
public:
    ConnectionPool(size_t maxSize) : maxSize_(maxSize) {}
    
    virtual ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!pool_.empty()) {
            pool_.pop();
        }
    }
    
    // 获取连接
    std::shared_ptr<Connection> acquire(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (!pool_.empty()) {
            auto conn = pool_.front();
            pool_.pop();
            return conn;
        }
        
        if (activeCount_ < maxSize_) {
            activeCount_++;
            auto conn = createConnection();
            return conn;
        }
        
        // 等待可用连接
        if (cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
            [this] { return !pool_.empty() || activeCount_ < maxSize_; })) {
            if (!pool_.empty()) {
                auto conn = pool_.front();
                pool_.pop();
                return conn;
            }
            if (activeCount_ < maxSize_) {
                activeCount_++;
                return createConnection();
            }
        }
        
        return nullptr;
    }
    
    // 释放连接
    void release(std::shared_ptr<Connection> conn) {
        if (!conn) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.size() < maxSize_) {
            pool_.push(conn);
        } else {
            activeCount_--;
        }
        cv_.notify_one();
    }
    
    // 池状态
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }
    
    size_t active() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeCount_;
    }

protected:
    virtual std::shared_ptr<Connection> createConnection() = 0;
    
    size_t maxSize_;
    size_t activeCount_ = 0;
    std::queue<std::shared_ptr<Connection>> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

// ============================================================================
// 批量操作处理器
// ============================================================================

template<typename Item, typename Result>
class BatchProcessor {
public:
    using ProcessorFunc = std::function<Result(const std::vector<Item>&)>;
    
    BatchProcessor(size_t batchSize, ProcessorFunc processor)
        : batchSize_(batchSize), processor_(processor) {}
    
    // 添加项目并执行批量处理
    std::vector<Result> add(const std::vector<Item>& items) {
        std::vector<Result> results;
        results.reserve(items.size());
        
        for (size_t i = 0; i < items.size(); i += batchSize_) {
            size_t end = std::min(i + batchSize_, items.size());
            std::vector<Item> batch(items.begin() + i, items.begin() + end);
            
            auto batchResult = processor_(batch);
            if constexpr (std::is_same_v<Result, bool>) {
                results.push_back(batchResult);
            } else {
                for (auto& r : batchResult) {
                    results.push_back(std::move(r));
                }
            }
        }
        
        return results;
    }
    
    // 并行批量处理
    std::vector<Result> parallel(const std::vector<Item>& items, int threads = 4) {
        std::vector<Result> results(items.size());
        std::vector<std::future<void>> futures;
        
        size_t itemsPerThread = (items.size() + threads - 1) / threads;
        
        for (int t = 0; t < threads; t++) {
            size_t start = t * itemsPerThread;
            size_t end = std::min(start + itemsPerThread, items.size());
            
            if (start >= items.size()) break;
            
            futures.push_back(std::async(std::launch::async, [&, start, end] {
                for (size_t i = start; i < end; i += batchSize_) {
                    size_t batchEnd = std::min(i + batchSize_, end);
                    std::vector<Item> batch(items.begin() + i, items.begin() + batchEnd);
                    auto batchResults = processor_(batch);
                    
                    for (size_t j = 0; j < batchResults.size() && i + j < end; j++) {
                        results[i + j] = std::move(batchResults[j]);
                    }
                }
            }));
        }
        
        for (auto& f : futures) {
            f.wait();
        }
        
        return results;
    }

private:
    size_t batchSize_;
    ProcessorFunc processor_;
};

// ============================================================================
// 查询缓存
// ============================================================================

class QueryCache {
public:
    struct CacheEntry {
        Json result;
        std::chrono::steady_clock::time_point expiry;
    };
    
    QueryCache(size_t maxSize, int ttlSeconds)
        : maxSize_(maxSize), ttlSeconds_(ttlSeconds) {}
    
    // 获取缓存
    std::optional<Json> get(const String& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            misses_++;
            return std::nullopt;
        }
        
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            cache_.erase(it);
            misses_++;
            return std::nullopt;
        }
        
        hits_++;
        return it->second.result;
    }
    
    // 设置缓存
    void set(const String& key, const Json& result) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (cache_.size() >= maxSize_) {
            // LRU: 移除最旧的
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.expiry < oldest->second.expiry) {
                    oldest = it;
                }
            }
            cache_.erase(oldest);
        }
        
        cache_[key] = {
            result,
            std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds_)
        };
    }
    
    // 清除缓存
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    
    // 统计
    double hitRate() const {
        size_t total = hits_ + misses_;
        return total > 0 ? static_cast<double>(hits_) / total : 0.0;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

private:
    size_t maxSize_;
    int ttlSeconds_;
    std::unordered_map<String, CacheEntry> cache_;
    mutable std::mutex mutex_;
    
    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;
};

// ============================================================================
// 异步任务队列
// ============================================================================

class AsyncTaskQueue {
public:
    using Task = std::function<void()>;
    
    AsyncTaskQueue(size_t numThreads = 4) {
        for (size_t i = 0; i < numThreads; i++) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }
    
    ~AsyncTaskQueue() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }
    
    // 提交任务
    std::future<void> submit(Task task) {
        auto promise = std::make_shared<std::promise<void>>();
        auto future = promise->get_future();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push([promise, task = std::move(task)] {
                try {
                    task();
                    promise->set_value();
                } catch (const std::exception&) {
                    promise->set_exception(std::current_exception());
                }
            });
        }
        cv_.notify_one();
        
        return future;
    }
    
    // 队列大小
    size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

private:
    void workerLoop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return shutdown_ || !tasks_.empty(); });
                
                if (shutdown_ && tasks_.empty()) return;
                
                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
            }
            
            if (task) task();
        }
    }
    
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;
};

// ============================================================================
// 性能监控
// ============================================================================

class PerformanceMonitor {
public:
    struct Metric {
        size_t count = 0;
        double totalTime = 0.0;
        double minTime = std::numeric_limits<double>::max();
        double maxTime = 0.0;
        
        void record(double timeMs) {
            count++;
            totalTime += timeMs;
            minTime = std::min(minTime, timeMs);
            maxTime = std::max(maxTime, timeMs);
        }
        
        double avgTime() const {
            return count > 0 ? totalTime / count : 0.0;
        }
    };
    
    // 记录操作
    void record(const String& operation, double timeMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_[operation].record(timeMs);
    }
    
    // 计时辅助类
    class Timer {
    public:
        Timer(PerformanceMonitor& monitor, const String& operation)
            : monitor_(monitor), operation_(operation),
              start_(std::chrono::high_resolution_clock::now()) {}
        
        ~Timer() {
            auto end = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration<double, std::milli>(end - start_).count();
            monitor_.record(operation_, ms);
        }
        
    private:
        PerformanceMonitor& monitor_;
        String operation_;
        std::chrono::high_resolution_clock::time_point start_;
    };
    
    // 获取指标
    std::unordered_map<String, Metric> getMetrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return metrics_;
    }
    
    // 转换为 JSON
    Json toJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Json j;
        for (const auto& [name, metric] : metrics_) {
            j[name] = {
                {"count", metric.count},
                {"avgMs", metric.avgTime()},
                {"minMs", metric.minTime == std::numeric_limits<double>::max() ? 0 : metric.minTime},
                {"maxMs", metric.maxTime}
            };
        }
        return j;
    }
    
    // 清除
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.clear();
    }

private:
    std::unordered_map<String, Metric> metrics_;
    mutable std::mutex mutex_;
};

// 全局监控实例
inline PerformanceMonitor& globalMonitor() {
    static PerformanceMonitor instance;
    return instance;
}

} // namespace ontology
