#pragma once
#include <claude/core/Types.hpp>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace claude {

/// Model pricing tier
struct ModelPricing {
    double inputPerMtok = 0.0;      // $ per million input tokens
    double outputPerMtok = 0.0;     // $ per million output tokens
    double cacheWritePerMtok = 0.0; // $ per million cache write tokens
    double cacheReadPerMtok = 0.0;  // $ per million cache read tokens
    double webSearchPerRequest = 0.01;
};

/// Per-model usage tracking
struct ModelUsage {
    long inputTokens = 0;
    long outputTokens = 0;
    long cacheReadTokens = 0;
    long cacheWriteTokens = 0;
    long webSearchRequests = 0;
    double costUSD = 0.0;
    int contextWindow = 200000;
    int maxOutputTokens = 8192;
};

/// Cost tracking service
class CostTracker {
public:
    static CostTracker& instance();

    /// Calculate cost for a single API call
    static double calculateCost(const String& model, const Usage& usage);

    /// Calculate cost with cache tokens
    static double calculateCostWithCache(const String& model,
        long inputTokens, long outputTokens,
        long cacheReadTokens = 0, long cacheWriteTokens = 0,
        long webSearchRequests = 0);

    /// Record usage and cost
    void recordUsage(const String& model, const Usage& usage, double costUSD);
    void recordUsageWithCache(const String& model,
        long inputTokens, long outputTokens,
        long cacheReadTokens, long cacheWriteTokens,
        long webSearchRequests, double costUSD);

    /// Query
    double getTotalCost() const;
    const std::unordered_map<String, ModelUsage>& getModelUsage() const;
    ModelUsage getUsageForModel(const String& model) const;
    bool hasUnknownModelCost() const { return hasUnknownModelCost_; }

    /// Format cost for display
    static String formatCost(double costUSD);
    String formatTotalCost() const;
    String formatModelBreakdown() const;

    /// Cache savings metrics
    struct CacheSavings {
        long totalCacheReadTokens = 0;
        long totalCacheWriteTokens = 0;
        long totalInputTokens = 0;
        double cacheReadCostSaved = 0.0;   // $ saved by reading from cache vs full input
        double cacheWriteCost = 0.0;       // $ spent on cache writes
        double savingsPercent = 0.0;       // % of input tokens served from cache
    };
    CacheSavings getCacheSavings() const;
    String formatCacheSavings() const;

    /// Reset
    void reset();

    /// Get pricing for a model
    static ModelPricing getPricing(const String& model);

    /// Resolve model alias to canonical name
    static String canonicalizeModelName(const String& model);

    /// Register custom model pricing
    static void registerPricing(const String& model, const ModelPricing& pricing);

private:
    CostTracker() = default;

    mutable std::mutex mutex_;
    std::unordered_map<String, ModelUsage> modelUsage_;
    double totalCost_ = 0.0;
    bool hasUnknownModelCost_ = false;

    static std::unordered_map<String, ModelPricing>& pricingTable();
};

} // namespace claude
