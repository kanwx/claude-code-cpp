#include <claude/core/StreamingToolExecutor.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>

namespace claude {

// ========== Construction ==========

StreamingToolExecutor::StreamingToolExecutor(
    ToolRegistry& tools,
    ToolContext& context,
    HookManager& hooks,
    RuleEngine* permissionEngine
)
    : tools_(tools)
    , context_(context)
    , hooks_(hooks)
    , permissionEngine_(permissionEngine)
{}

// ========== Public API ==========

std::vector<ToolExecutionResult> StreamingToolExecutor::execute(
    const std::vector<ToolCall>& toolCalls
) {
    return executeWithOrder(toolCalls, ToolExecutionOrder::ParallelReadOnly);
}

std::vector<ToolExecutionResult> StreamingToolExecutor::executeWithOrder(
    const std::vector<ToolCall>& toolCalls,
    ToolExecutionOrder order
) {
    if (toolCalls.empty()) return {};

    // Reset cancellation state at the start of each batch
    cancelled_.store(false, std::memory_order_relaxed);
    executing_.store(true, std::memory_order_release);

    // Result vector pre-allocated to preserve ordering by index
    std::vector<ToolExecutionResult> results(toolCalls.size());

    if (order == ToolExecutionOrder::Sequential) {
        // ---------- Full sequential ----------
        for (size_t i = 0; i < toolCalls.size(); ++i) {
            if (cancelled_.load(std::memory_order_relaxed)) {
                spdlog::debug("StreamingToolExecutor: cancelled at sequential index {}", i);
                // Fill remaining slots with error results
                for (size_t j = i; j < toolCalls.size(); ++j) {
                    results[j] = {
                        ToolResponse{toolCalls[j].id, toolCalls[j].name,
                                     "Cancelled by user", false, true, false},
                        std::chrono::milliseconds{0}, false, static_cast<int>(j)
                    };
                }
                break;
            }
            results[i] = executeSingle(toolCalls[i], static_cast<int>(i), /*parallel=*/false);
        }
    } else if (order == ToolExecutionOrder::FullParallel) {
        // ---------- Full parallel (unsafe) ----------
        // Partition into batches of maxParallelism_ to bound concurrency
        size_t total = toolCalls.size();
        size_t batchSize = static_cast<size_t>(maxParallelism_);
        if (batchSize == 0) batchSize = 1;

        for (size_t batchStart = 0; batchStart < total; batchStart += batchSize) {
            if (cancelled_.load(std::memory_order_relaxed)) {
                spdlog::debug("StreamingToolExecutor: cancelled at batch start {}", batchStart);
                for (size_t j = batchStart; j < total; ++j) {
                    results[j] = {
                        ToolResponse{toolCalls[j].id, toolCalls[j].name,
                                     "Cancelled by user", false, true, false},
                        std::chrono::milliseconds{0}, true, static_cast<int>(j)
                    };
                }
                break;
            }

            size_t batchEnd = std::min(batchStart + batchSize, total);
            std::vector<std::pair<ToolCall, int>> batch;
            batch.reserve(batchEnd - batchStart);
            for (size_t i = batchStart; i < batchEnd; ++i) {
                batch.emplace_back(toolCalls[i], static_cast<int>(i));
            }

            auto batchResults = executeParallel(batch);
            for (size_t k = 0; k < batchResults.size(); ++k) {
                results[batchStart + k] = std::move(batchResults[k]);
            }
        }
    } else {
        // ---------- ParallelReadOnly (default) ----------
        // Partition: parallel-safe vs sequential
        std::vector<std::pair<ToolCall, int>> parallelGroup;
        std::vector<std::pair<ToolCall, int>> sequentialGroup;

        for (size_t i = 0; i < toolCalls.size(); ++i) {
            if (isParallelSafe(toolCalls[i])) {
                parallelGroup.emplace_back(toolCalls[i], static_cast<int>(i));
            } else {
                sequentialGroup.emplace_back(toolCalls[i], static_cast<int>(i));
            }
        }

        // Execute parallel-safe group first
        if (!parallelGroup.empty()) {
            // Further sub-batch to respect maxParallelism_
            size_t batchSize = static_cast<size_t>(maxParallelism_);
            if (batchSize == 0) batchSize = 1;

            for (size_t batchStart = 0; batchStart < parallelGroup.size(); batchStart += batchSize) {
                if (cancelled_.load(std::memory_order_relaxed)) {
                    // Mark remaining parallel calls as cancelled
                    for (size_t j = batchStart; j < parallelGroup.size(); ++j) {
                        int idx = parallelGroup[j].second;
                        results[idx] = {
                            ToolResponse{parallelGroup[j].first.id, parallelGroup[j].first.name,
                                         "Cancelled by user", false, true, false},
                            std::chrono::milliseconds{0}, true, idx
                        };
                    }
                    break;
                }

                size_t batchEnd = std::min(batchStart + batchSize, parallelGroup.size());
                std::vector<std::pair<ToolCall, int>> batch(
                    parallelGroup.begin() + static_cast<ptrdiff_t>(batchStart),
                    parallelGroup.begin() + static_cast<ptrdiff_t>(batchEnd)
                );

                auto batchResults = executeParallel(batch);
                for (size_t k = 0; k < batchResults.size(); ++k) {
                    int idx = batchResults[k].executionOrder;
                    results[idx] = std::move(batchResults[k]);
                }
            }
        }

        // Execute sequential group one at a time
        for (const auto& [call, idx] : sequentialGroup) {
            if (cancelled_.load(std::memory_order_relaxed)) {
                results[idx] = {
                    ToolResponse{call.id, call.name, "Cancelled by user", false, true, false},
                    std::chrono::milliseconds{0}, false, idx
                };
                continue;
            }
            results[idx] = executeSingle(call, idx, /*parallel=*/false);
        }
    }

    // Apply aggregate budget truncation across all results
    applyAggregateTruncation(results);

    executing_.store(false, std::memory_order_release);
    return results;
}

void StreamingToolExecutor::cancel() {
    cancelled_.store(true, std::memory_order_relaxed);
    spdlog::debug("StreamingToolExecutor: cancellation requested");
}

bool StreamingToolExecutor::isExecuting() const {
    return executing_.load(std::memory_order_acquire);
}

int StreamingToolExecutor::activeToolCount() const {
    return activeCount_.load(std::memory_order_acquire);
}

// ========== Incremental enqueue API ==========

void StreamingToolExecutor::enqueue(ToolCall call, int index) {
    if (cancelled_.load(std::memory_order_acquire)) {
        // Immediately resolve with a cancelled result
        std::promise<ToolExecutionResult> promise;
        promise.set_value({
            ToolResponse{call.id, call.name, "Cancelled by user", false, true, false},
            std::chrono::milliseconds{0}, false, index
        });
        pendingFutures_.push_back({index, promise.get_future()});
        return;
    }

    // Dispatch via std::async, reusing the existing executeSingle()
    auto future = std::async(std::launch::async,
        [this, call = std::move(call), index]() -> ToolExecutionResult {
            return executeSingle(call, index, /*parallel=*/true);
        }
    );

    pendingFutures_.push_back({index, std::move(future)});
    spdlog::debug("StreamingToolExecutor: enqueued tool [{}] at index {}", call.name, index);
}

std::vector<ToolExecutionResult> StreamingToolExecutor::collectResults() {
    auto pending = std::move(pendingFutures_);
    pendingFutures_.clear();

    // Wait for all futures and collect, keyed by index for ordering
    std::map<int, ToolExecutionResult> ordered;
    for (auto& p : pending) {
        try {
            auto result = p.future.get();
            ordered[p.index] = std::move(result);
        } catch (const std::exception& e) {
            // Shouldn't happen (executeSingle catches all exceptions),
            // but handle defensively
            ToolExecutionResult errResult;
            errResult.response.content = "Error: " + String(e.what());
            errResult.response.isError = true;
            errResult.executionOrder = p.index;
            ordered[p.index] = std::move(errResult);
        }
    }

    // Return in index order
    std::vector<ToolExecutionResult> results;
    results.reserve(ordered.size());
    for (auto& [idx, result] : ordered) {
        results.push_back(std::move(result));
    }

    // Apply aggregate budget truncation
    applyAggregateTruncation(results);

    return results;
}

// ========== Parallel-safe classification ==========

bool StreamingToolExecutor::isParallelSafe(const ToolCall& call) const {
    Tool* tool = tools_.findByName(call.name);
    if (!tool) return false;

    // Parse input to check concurrency safety with actual arguments
    Json input;
    try {
        input = Json::parse(call.arguments);
    } catch (const Json::parse_error&) {
        // If we can't parse arguments, conservatively treat as sequential
        return false;
    }

    // Read-only AND concurrency-safe → always parallel
    if (tool->isReadOnly() && tool->isConcurrencySafe(input)) return true;

    // Non-blocking AND concurrency-safe → parallel (e.g. background Agent)
    if (tool->isNonBlocking(input) && tool->isConcurrencySafe(input)) return true;

    return false;
}

// ========== Single tool execution ==========

ToolExecutionResult StreamingToolExecutor::executeSingle(
    const ToolCall& call,
    int order,
    bool parallel
) {
    auto startTime = std::chrono::steady_clock::now();

    activeCount_.fetch_add(1, std::memory_order_relaxed);

    // Fire onToolStart callback
    Tool* tool = tools_.findByName(call.name);
    String description = tool ? tool->activityDescription(
        [&]() -> Json {
            try { return Json::parse(call.arguments); }
            catch (...) { return Json::object(); }
        }()
    ) : ("Running " + call.name + "...");
    if (onToolStart_) {
        onToolStart_(call.name, description, call.id);
    }

    // Parse input
    Json input;
    try {
        input = Json::parse(call.arguments);
    } catch (const Json::parse_error& e) {
        activeCount_.fetch_sub(1, std::memory_order_relaxed);
        if (onToolComplete_) onToolComplete_(call.name, false);
        return {
            ToolResponse{call.id, call.name,
                         "Error: Invalid JSON arguments: " + String(e.what()), true},
            std::chrono::milliseconds{0}, parallel, order
        };
    }

    // Unknown tool
    if (!tool) {
        activeCount_.fetch_sub(1, std::memory_order_relaxed);
        if (onToolComplete_) onToolComplete_(call.name, false);
        return {
            ToolResponse{call.id, call.name,
                         "Error: Unknown tool '" + call.name + "'", true},
            std::chrono::milliseconds{0}, parallel, order
        };
    }

    // ====== PreToolUse Hook ======
    HookContext preCtx;
    preCtx.toolName = call.name;
    preCtx.input = input;
    HookResult preResult = runPreToolUseHooks(call.name, input, preCtx);

    if (preResult .shouldAbort()) {
        activeCount_.fetch_sub(1, std::memory_order_relaxed);
        if (onToolComplete_) onToolComplete_(call.name, false);
        spdlog::debug("StreamingToolExecutor: tool [{}] blocked by PreToolUse hook", call.name);
        return {
            ToolResponse{call.id, call.name,
                         "Tool blocked by pre-tool hook", false, false, true},
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime),
            parallel, order
        };
    }

    // ====== Permission check ======
    // Check hook permission override first
    auto permOverride = preCtx.getPermissionOverride();
    bool permissionGranted = false;
    String denyReason;

    if (permOverride && *permOverride) {
        // Hook forced allow: skip permission check
        permissionGranted = true;
        spdlog::debug("StreamingToolExecutor: permission overridden (allow) by hook for [{}]", call.name);
    } else if (permOverride && !*permOverride) {
        // Hook forced deny
        permissionGranted = false;
        denyReason = "Permission denied by hook";
        spdlog::debug("StreamingToolExecutor: permission overridden (deny) by hook for [{}]", call.name);
    } else {
        // Normal permission flow — must serialize access to permissionEngine_
        // because RuleEngine::evaluate and applyChoice are not thread-safe
        std::lock_guard<std::mutex> lock(permissionMutex_);
        auto decision = checkPermissions(call.name, input, tool->isReadOnly());

        if (decision.isDenied()) {
            permissionGranted = false;
            denyReason = "Permission denied: " + decision.reason;
        } else if (decision.needsAsk()) {
            if (onPermissionRequest_) {
                PermissionRequest req{call.name, call.arguments, tool->activityDescription(input), decision};
                auto choice = onPermissionRequest_(req);

                if (choice == PermissionChoice::DenyOnce || choice == PermissionChoice::AlwaysDeny) {
                    permissionGranted = false;
                    denyReason = "Permission denied by user";
                } else {
                    permissionGranted = true;
                    // Apply the user's choice to the rule engine for future calls
                    String command = input.value("command", input.value("file_path", ""));
                    if (permissionEngine_) {
                        permissionEngine_->applyChoice(choice, call.name, command);
                    }
                }
            } else {
                // No callback registered: deny by default for ask decisions
                permissionGranted = false;
                denyReason = "Permission required but no handler registered";
            }
        } else {
            // Decision is Allow
            permissionGranted = true;
        }
    }

    if (!permissionGranted) {
        activeCount_.fetch_sub(1, std::memory_order_relaxed);
        if (onToolComplete_) onToolComplete_(call.name, false);
        return {
            ToolResponse{call.id, call.name, denyReason, false, false, true},
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime),
            parallel, order
        };
    }

    // ====== Cancellation check before execute ======
    if (cancelled_.load(std::memory_order_relaxed)) {
        activeCount_.fetch_sub(1, std::memory_order_relaxed);
        if (onToolComplete_) onToolComplete_(call.name, false);
        return {
            ToolResponse{call.id, call.name, "Cancelled by user", false, true, false},
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime),
            parallel, order
        };
    }

    // ====== Execute the tool ======
    String result;
    bool isError = false;
    try {
        if (tool->supportsStreaming() && onToolChunk_) {
            result = tool->executeStreaming(input, context_,
                [this](const String& chunk) -> bool {
                    if (onToolChunk_) onToolChunk_(chunk);
                    return !cancelled_.load(std::memory_order_relaxed);
                });
        } else {
            result = tool->execute(input, context_);
        }
    } catch (const std::exception& e) {
        result = "Error: " + String(e.what());
        isError = true;
    } catch (...) {
        result = "Error: Unknown exception during tool execution";
        isError = true;
    }

    // ====== Per-tool result truncation ======
    if (result.size() > tool->maxResultSizeChars()) {
        spdlog::debug("StreamingToolExecutor: tool [{}] result size {} exceeds budget {}, truncating",
            call.name, result.size(), tool->maxResultSizeChars());
        result = ResultTruncation::truncate(result, tool->maxResultSizeChars(), call.name);
    }

    // ====== PostToolUse Hook ======
    HookContext postCtx;
    postCtx.toolName = call.name;
    postCtx.input = input;
    postCtx.result = result;
    runPostToolUseHooks(call.name, input, result, postCtx);

    if (postCtx.result && *postCtx.result != result) {
        result = *postCtx.result;
    }

    // ====== Post-execution cancellation check ======
    // If cancellation was requested while the tool was executing,
    // mark the response as cancelled so the UI renders it correctly.
    bool wasCancelled = cancelled_.load(std::memory_order_relaxed);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);

    activeCount_.fetch_sub(1, std::memory_order_relaxed);
    if (onToolComplete_) onToolComplete_(call.name, !isError);

    return {
        ToolResponse{call.id, call.name, result, isError, wasCancelled, false},
        duration, parallel, order
    };
}

// ========== Permission check ==========

PermissionDecision StreamingToolExecutor::checkPermissions(
    const String& toolName,
    const Json& input,
    bool isReadOnly
) {
    // Caller must hold permissionMutex_

    if (!permissionEngine_) {
        // No engine: default allow for read-only, ask for write tools
        if (isReadOnly) {
            return PermissionDecision::allow("Read-only tool, no permission engine");
        }
        return PermissionDecision::ask(toolName, "");
    }

    // Evaluate with transcript if available (for YOLO classifier)
    if (transcript_) {
        return permissionEngine_->evaluate(toolName, input, isReadOnly, *transcript_);
    }
    return permissionEngine_->evaluate(toolName, input, isReadOnly);
}

// ========== Hook execution ==========

HookResult StreamingToolExecutor::runPreToolUseHooks(
    const String& toolName,
    const Json& input,
    HookContext& ctx
) {
    ctx.toolName = toolName;
    ctx.input = input;
    return hooks_.execute(HookType::PreToolUse, ctx);
}

void StreamingToolExecutor::runPostToolUseHooks(
    const String& toolName,
    const Json& input,
    const String& result,
    HookContext& ctx
) {
    ctx.toolName = toolName;
    ctx.input = input;
    ctx.result = result;
    // PostToolUse hooks run even if the tool errored, matching TS behavior
    // A hook returning Abort here does NOT suppress the tool result from
    // the conversation, but it can modify it.
    hooks_.execute(HookType::PostToolUse, ctx);
}

// ========== Parallel batch execution ==========

std::vector<ToolExecutionResult> StreamingToolExecutor::executeParallel(
    const std::vector<std::pair<ToolCall, int>>& calls
) {
    if (calls.empty()) return {};

    // Shared results array: each index is written by exactly one thread.
    // Safe because no two threads access the same index, and we join all
    // threads (via future::get) before reading any slot.
    auto sharedResults = std::make_shared<std::vector<ToolExecutionResult>>(calls.size());
    std::vector<std::future<void>> futures;
    futures.reserve(calls.size());

    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& [call, order] = calls[i];
        ToolCall callCopy = call;
        int orderCopy = order;

        futures.push_back(std::async(std::launch::async,
            [this, i, callCopy = std::move(callCopy), orderCopy, sharedResults]() {
                auto result = executeSingle(callCopy, orderCopy, /*parallel=*/true);
                (*sharedResults)[i] = std::move(result);
            }
        ));
    }

    // Wait for all futures, handling exceptions
    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            futures[i].get();
        } catch (const std::exception& e) {
            const auto& call = calls[i].first;
            (*sharedResults)[i] = {
                ToolResponse{call.id, call.name,
                             "Error: " + String(e.what()), true},
                std::chrono::milliseconds{0}, true, calls[i].second
            };
        } catch (...) {
            const auto& call = calls[i].first;
            (*sharedResults)[i] = {
                ToolResponse{call.id, call.name,
                             "Error: Unknown exception in parallel execution", true},
                std::chrono::milliseconds{0}, true, calls[i].second
            };
        }
    }

    return std::move(*sharedResults);
}

// ========== Aggregate truncation ==========

void StreamingToolExecutor::applyAggregateTruncation(std::vector<ToolExecutionResult>& results) {
    size_t totalSize = 0;
    for (const auto& r : results) {
        totalSize += r.response.content.size();
    }

    if (totalSize <= ResultTruncation::AGGREGATE_BUDGET) {
        return;
    }

    spdlog::debug("StreamingToolExecutor: aggregate tool result size {} exceeds budget {}, truncating oldest",
        totalSize, ResultTruncation::AGGREGATE_BUDGET);

    // Truncate from oldest (lowest index) first
    for (size_t i = 0; i < results.size() && totalSize > ResultTruncation::AGGREGATE_BUDGET; ++i) {
        auto& content = results[i].response.content;
        if (content.size() <= ResultTruncation::HEAD_CHARS + ResultTruncation::TAIL_CHARS + 200) {
            continue; // Already small
        }
        size_t oldSize = content.size();
        content = ResultTruncation::truncate(content,
            ResultTruncation::HEAD_CHARS + ResultTruncation::TAIL_CHARS + 200,
            results[i].response.toolName);
        totalSize -= (oldSize - content.size());
    }
}

} // namespace claude
