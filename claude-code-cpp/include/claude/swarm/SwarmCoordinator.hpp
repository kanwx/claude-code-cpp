#pragma once

#include "../core/Types.hpp"
#include <functional>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

namespace claude {

// Forward declarations
class ApiClient;
class AgentLoop;
class ToolRegistry;

} // namespace claude

namespace claude::swarm {

using Json = nlohmann::json;

/// Agent状态
enum class AgentStatus {
    Idle,
    Working,
    Waiting,
    Completed,
    Failed
};

/// 任务状态
enum class TaskStatus {
    Pending,
    Assigned,
    InProgress,
    Completed,
    Failed,
    Cancelled
};

/// 任务优先级
enum class Priority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

/// Agent信息
struct AgentInfo {
    String id;
    String name;
    String type;            // "coder", "reviewer", "tester", etc.
    AgentStatus status = AgentStatus::Idle;
    std::vector<String> capabilities;  // 能力标签
    int maxConcurrentTasks = 1;
    int currentTasks = 0;
    Json metadata;

    bool canHandle(const String& capability) const {
        return std::find(capabilities.begin(), capabilities.end(), capability) != capabilities.end();
    }

    bool isAvailable() const {
        return status == AgentStatus::Idle && currentTasks < maxConcurrentTasks;
    }
};

/// 任务定义
struct TaskDefinition {
    String id;
    String name;
    String description;
    String requiredCapability;
    Priority priority = Priority::Normal;
    TaskStatus status = TaskStatus::Pending;
    String assignedAgent;
    Json input;
    Json output;
    String error;
    std::vector<String> dependencies;  // 依赖任务ID
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point startedAt;
    std::chrono::system_clock::time_point completedAt;
    int timeout = 300;  // 秒

    Json toJson() const;
};

/// 任务结果
struct TaskResult {
    String taskId;
    bool success;
    Json output;
    String error;
    int durationMs;
};

/// Swarm配置
struct SwarmConfig {
    int maxAgents = 10;
    int maxTasks = 100;
    int taskTimeout = 300;          // 秒
    int retryAttempts = 3;
    bool enableParallelism = true;
    bool enableRetry = true;
    int heartbeatInterval = 30;     // 秒
};

/// 任务完成回调
using TaskCallback = std::function<void(const TaskResult& result)>;

/// Swarm协调器
class SwarmCoordinator {
public:
    static SwarmCoordinator& instance() {
        static SwarmCoordinator coordinator;
        return coordinator;
    }

    /// 初始化
    void initialize(const SwarmConfig& config);

    /// Set the API client for local task execution (creates real AgentLoop instances)
    void setApiClient(ApiClient* client) { apiClient_ = client; }

    /// 关闭
    void shutdown();

    // ========== Agent管理 ==========

    /// 注册Agent
    String registerAgent(const AgentInfo& agent);

    /// 注销Agent
    bool unregisterAgent(const String& agentId);

    /// 更新Agent状态
    bool updateAgentStatus(const String& agentId, AgentStatus status);

    /// 获取Agent信息
    std::optional<AgentInfo> getAgent(const String& agentId) const;

    /// 列出所有Agent
    std::vector<AgentInfo> listAgents() const;

    /// 查找可用Agent
    std::vector<AgentInfo> findAvailableAgents(const String& capability = "") const;

    // ========== 任务管理 ==========

    /// 提交任务
    String submitTask(const TaskDefinition& task, TaskCallback callback = nullptr);

    /// 取消任务
    bool cancelTask(const String& taskId);

    /// 获取任务状态
    std::optional<TaskDefinition> getTask(const String& taskId) const;

    /// 列出任务
    std::vector<TaskDefinition> listTasks(TaskStatus status = TaskStatus::Pending) const;

    /// 分配任务
    bool assignTask(const String& taskId, const String& agentId);

    /// 完成任务
    bool completeTask(const String& taskId, const Json& output);

    /// 任务失败
    bool failTask(const String& taskId, const String& error);

    // ========== 执行 ==========

    /// 启动协调器
    void start();

    /// 停止协调器
    void stop();

    /// 是否运行中
    bool isRunning() const { return running_; }

    /// 等待所有任务完成
    void waitForCompletion();

    /// 等待特定任务完成
    std::optional<TaskResult> waitForTask(const String& taskId, int timeoutMs = -1);

    // ========== 统计 ==========

    /// 获取统计
    Json getStats() const;

    /// 获取进度
    double getProgress() const;

private:
    SwarmCoordinator() = default;

    SwarmConfig config_;
    ApiClient* apiClient_ = nullptr;
    std::atomic<bool> running_{false};
    std::unordered_map<String, AgentInfo> agents_;
    std::unordered_map<String, TaskDefinition> tasks_;
    std::unordered_map<String, TaskCallback> callbacks_;
    std::queue<String> taskQueue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread workerThread_;

    /// 工作线程
    void workerLoop();

    /// 调度任务
    void scheduleTasks();

    /// 检查依赖
    bool checkDependencies(const TaskDefinition& task) const;

    /// 检查超时
    void checkTimeouts();

    /// 执行任务
    TaskResult executeTask(TaskDefinition& task, AgentInfo& agent);

    /// 远程任务执行
    TaskResult executeRemoteTask(TaskDefinition& task, AgentInfo& agent,
                                  const String& endpoint, const Json& request);

    /// 本地任务执行
    TaskResult executeLocalTask(TaskDefinition& task, AgentInfo& agent,
                                 const Json& request);

    /// 代码任务执行
    TaskResult executeCodeTask(TaskDefinition& task, AgentInfo& agent,
                                const Json& request);

    /// 审查任务执行
    TaskResult executeReviewTask(TaskDefinition& task, AgentInfo& agent,
                                  const Json& request);

    /// 测试任务执行
    TaskResult executeTestTask(TaskDefinition& task, AgentInfo& agent,
                                const Json& request);

    /// 生成ID
    String generateId(const String& prefix);
};

/// Swarm任务构建器
class TaskBuilder {
public:
    TaskBuilder& name(const String& name) { task_.name = name; return *this; }
    TaskBuilder& description(const String& desc) { task_.description = desc; return *this; }
    TaskBuilder& capability(const String& cap) { task_.requiredCapability = cap; return *this; }
    TaskBuilder& priority(Priority p) { task_.priority = p; return *this; }
    TaskBuilder& input(const Json& input) { task_.input = input; return *this; }
    TaskBuilder& timeout(int seconds) { task_.timeout = seconds; return *this; }
    TaskBuilder& dependsOn(const String& taskId) { task_.dependencies.push_back(taskId); return *this; }

    TaskDefinition build() { return task_; }

private:
    TaskDefinition task_;
};

} // namespace claude::swarm
