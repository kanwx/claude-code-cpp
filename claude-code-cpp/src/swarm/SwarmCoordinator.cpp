#include <claude/swarm/SwarmCoordinator.hpp>
#include <claude/api/ApiClient.hpp>
#include <claude/core/AgentLoop.hpp>
#include <claude/tool/ToolRegistry.hpp>
#include <claude/tool/AgentTypes.hpp>
#include <httplib.h>
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>

namespace claude::swarm {

// ========== TaskDefinition ==========

Json TaskDefinition::toJson() const {
    return {
        {"id", id},
        {"name", name},
        {"description", description},
        {"required_capability", requiredCapability},
        {"priority", static_cast<int>(priority)},
        {"status", static_cast<int>(status)},
        {"assigned_agent", assignedAgent},
        {"input", input},
        {"output", output},
        {"error", error},
        {"dependencies", dependencies}
    };
}

// ========== SwarmCoordinator ==========

void SwarmCoordinator::initialize(const SwarmConfig& config) {
    config_ = config;
}

void SwarmCoordinator::shutdown() {
    stop();
}

// ========== Agent管理 ==========

String SwarmCoordinator::registerAgent(const AgentInfo& agent) {
    std::lock_guard<std::mutex> lock(mutex_);

    String id = agent.id.empty() ? generateId("agent") : agent.id;
    agents_[id] = agent;
    agents_[id].id = id;

    return id;
}

bool SwarmCoordinator::unregisterAgent(const String& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = agents_.find(agentId);
    if (it == agents_.end()) return false;

    // 检查是否有进行中的任务
    for (auto& [tid, task] : tasks_) {
        if (task.assignedAgent == agentId && task.status == TaskStatus::InProgress) {
            task.status = TaskStatus::Pending;
            task.assignedAgent.clear();
            taskQueue_.push(tid);
        }
    }

    agents_.erase(it);
    return true;
}

bool SwarmCoordinator::updateAgentStatus(const String& agentId, AgentStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = agents_.find(agentId);
    if (it == agents_.end()) return false;

    it->second.status = status;
    return true;
}

std::optional<AgentInfo> SwarmCoordinator::getAgent(const String& agentId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = agents_.find(agentId);
    if (it == agents_.end()) return std::nullopt;
    return it->second;
}

std::vector<AgentInfo> SwarmCoordinator::listAgents() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<AgentInfo> result;
    for (const auto& [id, agent] : agents_) {
        result.push_back(agent);
    }
    return result;
}

std::vector<AgentInfo> SwarmCoordinator::findAvailableAgents(const String& capability) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<AgentInfo> result;
    for (const auto& [id, agent] : agents_) {
        if (agent.isAvailable()) {
            if (capability.empty() || agent.canHandle(capability)) {
                result.push_back(agent);
            }
        }
    }
    return result;
}

// ========== 任务管理 ==========

String SwarmCoordinator::submitTask(const TaskDefinition& task, TaskCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    String id = task.id.empty() ? generateId("task") : task.id;
    tasks_[id] = task;
    tasks_[id].id = id;
    tasks_[id].status = TaskStatus::Pending;
    tasks_[id].createdAt = std::chrono::system_clock::now();

    if (callback) {
        callbacks_[id] = callback;
    }

    taskQueue_.push(id);
    cv_.notify_one();

    return id;
}

bool SwarmCoordinator::cancelTask(const String& taskId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;

    if (it->second.status == TaskStatus::InProgress) {
        // 无法取消进行中的任务
        return false;
    }

    it->second.status = TaskStatus::Cancelled;
    callbacks_.erase(taskId);
    return true;
}

std::optional<TaskDefinition> SwarmCoordinator::getTask(const String& taskId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return std::nullopt;
    return it->second;
}

std::vector<TaskDefinition> SwarmCoordinator::listTasks(TaskStatus status) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TaskDefinition> result;
    for (const auto& [id, task] : tasks_) {
        if (status == TaskStatus::Pending || task.status == status) {
            result.push_back(task);
        }
    }
    return result;
}

bool SwarmCoordinator::assignTask(const String& taskId, const String& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto taskIt = tasks_.find(taskId);
    if (taskIt == tasks_.end()) return false;

    auto agentIt = agents_.find(agentId);
    if (agentIt == agents_.end()) return false;

    if (!agentIt->second.isAvailable()) return false;

    taskIt->second.status = TaskStatus::Assigned;
    taskIt->second.assignedAgent = agentId;
    agentIt->second.currentTasks++;

    return true;
}

bool SwarmCoordinator::completeTask(const String& taskId, const Json& output) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;

    it->second.status = TaskStatus::Completed;
    it->second.output = output;
    it->second.completedAt = std::chrono::system_clock::now();

    // 更新Agent
    if (!it->second.assignedAgent.empty()) {
        auto agentIt = agents_.find(it->second.assignedAgent);
        if (agentIt != agents_.end()) {
            agentIt->second.currentTasks--;
            if (agentIt->second.currentTasks == 0) {
                agentIt->second.status = AgentStatus::Idle;
            }
        }
    }

    // 回调
    auto callbackIt = callbacks_.find(taskId);
    if (callbackIt != callbacks_.end()) {
        auto callback = callbackIt->second;
        TaskResult result;
        result.taskId = taskId;
        result.success = true;
        result.output = output;

        lock.~lock_guard();
        callback(result);
    }

    cv_.notify_all();
    return true;
}

bool SwarmCoordinator::failTask(const String& taskId, const String& error) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;

    it->second.status = TaskStatus::Failed;
    it->second.error = error;
    it->second.completedAt = std::chrono::system_clock::now();

    // 更新Agent
    if (!it->second.assignedAgent.empty()) {
        auto agentIt = agents_.find(it->second.assignedAgent);
        if (agentIt != agents_.end()) {
            agentIt->second.currentTasks--;
            if (agentIt->second.currentTasks == 0) {
                agentIt->second.status = AgentStatus::Idle;
            }
        }
    }

    // 回调
    auto callbackIt = callbacks_.find(taskId);
    if (callbackIt != callbacks_.end()) {
        auto callback = callbackIt->second;
        TaskResult result;
        result.taskId = taskId;
        result.success = false;
        result.error = error;

        lock.~lock_guard();
        callback(result);
    }

    cv_.notify_all();
    return true;
}

// ========== 执行 ==========

void SwarmCoordinator::start() {
    if (running_) return;
    running_ = true;
    workerThread_ = std::thread(&SwarmCoordinator::workerLoop, this);
}

void SwarmCoordinator::stop() {
    if (!running_) return;
    running_ = false;
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void SwarmCoordinator::waitForCompletion() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] {
        for (const auto& [id, task] : tasks_) {
            if (task.status == TaskStatus::Pending ||
                task.status == TaskStatus::Assigned ||
                task.status == TaskStatus::InProgress) {
                return false;
            }
        }
        return true;
    });
}

std::optional<TaskResult> SwarmCoordinator::waitForTask(const String& taskId, int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto pred = [this, &taskId] {
        auto it = tasks_.find(taskId);
        return it != tasks_.end() && (
            it->second.status == TaskStatus::Completed ||
            it->second.status == TaskStatus::Failed ||
            it->second.status == TaskStatus::Cancelled
        );
    };

    if (timeoutMs > 0) {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred)) {
            return std::nullopt;
        }
    } else {
        cv_.wait(lock, pred);
    }

    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return std::nullopt;

    TaskResult result;
    result.taskId = taskId;
    result.success = it->second.status == TaskStatus::Completed;
    result.output = it->second.output;
    result.error = it->second.error;

    return result;
}

// ========== 统计 ==========

Json SwarmCoordinator::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    int pending = 0, inProgress = 0, completed = 0, failed = 0;
    for (const auto& [id, task] : tasks_) {
        switch (task.status) {
            case TaskStatus::Pending: case TaskStatus::Assigned: pending++; break;
            case TaskStatus::InProgress: inProgress++; break;
            case TaskStatus::Completed: completed++; break;
            case TaskStatus::Failed: case TaskStatus::Cancelled: failed++; break;
        }
    }

    return {
        {"agents", agents_.size()},
        {"tasks", tasks_.size()},
        {"pending", pending},
        {"in_progress", inProgress},
        {"completed", completed},
        {"failed", failed},
        {"running", running_.load()}
    };
}

double SwarmCoordinator::getProgress() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tasks_.empty()) return 1.0;

    int completed = 0;
    for (const auto& [id, task] : tasks_) {
        if (task.status == TaskStatus::Completed ||
            task.status == TaskStatus::Failed ||
            task.status == TaskStatus::Cancelled) {
            completed++;
        }
    }

    return static_cast<double>(completed) / tasks_.size();
}

// ========== 私有方法 ==========

void SwarmCoordinator::workerLoop() {
    while (running_) {
        scheduleTasks();
        checkTimeouts();

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(1));
    }
}

void SwarmCoordinator::scheduleTasks() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 按优先级排序待处理任务
    std::vector<String> pendingTasks;
    while (!taskQueue_.empty()) {
        pendingTasks.push_back(taskQueue_.front());
        taskQueue_.pop();
    }

    std::sort(pendingTasks.begin(), pendingTasks.end(), [this](const String& a, const String& b) {
        auto ta = tasks_.find(a);
        auto tb = tasks_.find(b);
        if (ta == tasks_.end() || tb == tasks_.end()) return false;
        return static_cast<int>(ta->second.priority) > static_cast<int>(tb->second.priority);
    });

    for (const auto& taskId : pendingTasks) {
        auto& task = tasks_[taskId];
        if (task.status != TaskStatus::Pending) continue;
        if (!checkDependencies(task)) continue;

        // 找可用Agent
        auto available = findAvailableAgents(task.requiredCapability);
        if (available.empty()) {
            taskQueue_.push(taskId);
            continue;
        }

        // 分配任务
        auto& agent = available[0];
        task.status = TaskStatus::InProgress;
        task.assignedAgent = agent.id;
        task.startedAt = std::chrono::system_clock::now();

        agents_[agent.id].currentTasks++;
        agents_[agent.id].status = AgentStatus::Working;
    }
}

bool SwarmCoordinator::checkDependencies(const TaskDefinition& task) const {
    for (const auto& depId : task.dependencies) {
        auto it = tasks_.find(depId);
        if (it == tasks_.end()) return false;
        if (it->second.status != TaskStatus::Completed) return false;
    }
    return true;
}

void SwarmCoordinator::checkTimeouts() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::now();

    for (auto& [id, task] : tasks_) {
        if (task.status == TaskStatus::InProgress) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - task.startedAt).count();
            if (elapsed > task.timeout) {
                task.status = TaskStatus::Failed;
                task.error = "Task timeout";

                if (!task.assignedAgent.empty()) {
                    auto agentIt = agents_.find(task.assignedAgent);
                    if (agentIt != agents_.end()) {
                        agentIt->second.currentTasks--;
                        agentIt->second.status = AgentStatus::Idle;
                    }
                }
            }
        }
    }
}

TaskResult SwarmCoordinator::executeTask(TaskDefinition& task, AgentInfo& agent) {
    TaskResult result;
    result.taskId = task.id;

    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        // 构建Agent请求
        Json agentRequest = {
            {"task_id", task.id},
            {"task_name", task.name},
            {"description", task.description},
            {"input", task.input},
            {"agent_id", agent.id},
            {"agent_type", agent.type},
            {"timeout", task.timeout}
        };

        // 检查Agent通信端点
        String agentEndpoint = agent.metadata.value("endpoint", "");

        if (!agentEndpoint.empty()) {
            // 通过HTTP与远程Agent通信
            result = executeRemoteTask(task, agent, agentEndpoint, agentRequest);
        } else if (agent.type == "local" || agent.type == "builtin") {
            // 本地Agent执行
            result = executeLocalTask(task, agent, agentRequest);
        } else {
            // 模拟执行（用于测试）
            result.success = true;
            result.output = {
                {"message", "Task simulated by " + agent.name},
                {"task_id", task.id},
                {"agent", agent.name},
                {"type", "simulated"}
            };
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

TaskResult SwarmCoordinator::executeRemoteTask(TaskDefinition& task, AgentInfo& agent,
                                                const String& endpoint, const Json& request) {
    TaskResult result;
    result.taskId = task.id;

    // 使用httplib发送HTTP请求
    try {
        // 解析端点URL
        String host, path;
        int port = 443;
        bool useSSL = true;

        String url = endpoint;
        if (url.substr(0, 8) == "https://") {
            host = url.substr(8);
        } else if (url.substr(0, 7) == "http://") {
            host = url.substr(7);
            useSSL = false;
            port = 80;
        }

        auto slashPos = host.find('/');
        if (slashPos != String::npos) {
            path = host.substr(slashPos);
            host = host.substr(0, slashPos);
        } else {
            path = "/execute";
        }

        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"}
        };

        String apiKey = agent.metadata.value("api_key", "");
        if (!apiKey.empty()) {
            headers.insert({"Authorization", "Bearer " + apiKey});
        }

        httplib::Result res;
        String body = request.dump();

        if (useSSL) {
            httplib::SSLClient client(host, port);
            client.set_connection_timeout(task.timeout, 0);
            res = client.Post(path, headers, body, "application/json");
        } else {
            httplib::Client client(host, port);
            client.set_connection_timeout(task.timeout, 0);
            res = client.Post(path, headers, body, "application/json");
        }

        if (res) {
            try {
                result.output = Json::parse(res->body);
                result.success = result.output.value("success", false);
                if (!result.success) {
                    result.error = result.output.value("error", "Unknown error");
                }
            } catch (...) {
                result.success = false;
                result.error = "Invalid response from agent";
            }
        } else {
            result.success = false;
            result.error = "Failed to connect to agent: " + httplib::to_string(res.error());
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
    }

    return result;
}

TaskResult SwarmCoordinator::executeLocalTask(TaskDefinition& task, AgentInfo& agent,
                                               const Json& request) {
    TaskResult result;
    result.taskId = task.id;

    if (!apiClient_) {
        result.success = false;
        result.error = "No API client configured for SwarmCoordinator — call setApiClient() first";
        return result;
    }

    // Resolve agent type from task capability
    String agentTypeName = "worker";  // default
    if (task.requiredCapability == "review") agentTypeName = "code-review";
    else if (task.requiredCapability == "security") agentTypeName = "security-audit";
    else if (task.requiredCapability == "test") agentTypeName = "test-generator";
    else if (task.requiredCapability == "explore") agentTypeName = "Explore";
    else if (task.requiredCapability == "plan") agentTypeName = "Plan";

    auto typeDef = claude::AgentTypeRegistry::instance().getType(agentTypeName);
    if (!typeDef) {
        result.success = false;
        result.error = "Unknown agent type: " + agentTypeName;
        return result;
    }

    // Build prompt from task
    String prompt = task.description;
    if (task.input.contains("prompt")) {
        prompt = task.input["prompt"].get<String>();
    }

    auto startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    try {
        // Create isolated tool registry with allowed tools
        auto registry = std::make_unique<claude::ToolRegistry>();
        for (const auto& toolName : typeDef->allowedTools) {
            auto tool = claude::ToolRegistry::createToolByName(toolName);
            if (tool) registry->registerTool(std::move(tool));
        }

        // Create isolated agent loop
        auto tracker = std::make_unique<claude::TokenTracker>();
        auto agentLoop = std::make_unique<claude::AgentLoop>(
            *apiClient_, *registry, typeDef->systemPrompt, *tracker);
        agentLoop->setMaxIterations(typeDef->maxIterations);
        agentLoop->setTemperature(typeDef->temperature);
        agentLoop->setMaxTokensOverride(typeDef->maxTokens);

        // Execute
        auto loopResult = agentLoop->run(prompt);

        auto endMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        result.durationMs = static_cast<int>(endMs - startMs);

        if (loopResult) {
            result.success = true;
            // Format as <task-notification> XML for coordinator consumption
            std::ostringstream xml;
            xml << "<task-notification>\n";
            xml << "<task-id>" << task.id << "</task-id>\n";
            xml << "<status>completed</status>\n";
            xml << "<summary>" << agent.name << " completed: " << task.name << "</summary>\n";
            xml << "<result>" << *loopResult << "</result>\n";
            xml << "<usage>\n";
            xml << "  <total_tokens>" << tracker->getTotalTokens() << "</total_tokens>\n";
            xml << "  <tool_uses>" << registry->size() << "</tool_uses>\n";
            xml << "  <duration_ms>" << result.durationMs << "</duration_ms>\n";
            xml << "</usage>\n";
            xml << "</task-notification>";
            result.output = {{"notification", xml.str()}};
        } else {
            result.success = false;
            result.error = loopResult.error();
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.error = String("Agent execution failed: ") + e.what();
    }

    return result;
}

TaskResult SwarmCoordinator::executeCodeTask(TaskDefinition& task, AgentInfo& agent,
                                              const Json& request) {
    // Delegate to the unified executeLocalTask with worker agent
    task.requiredCapability = "code";
    return executeLocalTask(task, agent, request);
}

TaskResult SwarmCoordinator::executeReviewTask(TaskDefinition& task, AgentInfo& agent,
                                                const Json& request) {
    task.requiredCapability = "review";
    return executeLocalTask(task, agent, request);
}

TaskResult SwarmCoordinator::executeTestTask(TaskDefinition& task, AgentInfo& agent,
                                              const Json& request) {
    task.requiredCapability = "test";
    return executeLocalTask(task, agent, request);
}

String SwarmCoordinator::generateId(const String& prefix) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 999999);

    std::ostringstream oss;
    oss << prefix << "-" << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()
        << "-" << dis(gen);
    return oss.str();
}

} // namespace claude::swarm
