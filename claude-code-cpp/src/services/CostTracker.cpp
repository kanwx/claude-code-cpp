#include <claude/services/CostTracker.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace claude {

CostTracker& CostTracker::instance() {
    static CostTracker inst;
    return inst;
}

// Pricing table - prices in USD per million tokens
std::unordered_map<String, ModelPricing>& CostTracker::pricingTable() {
    static std::unordered_map<String, ModelPricing> table = {
        // Haiku
        {"claude-3-5-haiku", {0.80, 4.00, 1.00, 0.08, 0.01}},
        {"claude-haiku-4-5", {1.00, 5.00, 1.25, 0.10, 0.01}},
        {"claude-haiku-4-5-20251001", {1.00, 5.00, 1.25, 0.10, 0.01}},

        // Sonnet
        {"claude-3-5-sonnet", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"claude-3-5-sonnet-v2", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"claude-3-7-sonnet", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"claude-sonnet-4", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"claude-sonnet-4-5", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"claude-sonnet-4-5-20250929", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"claude-sonnet-4-6", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"sonnet", {3.00, 15.00, 3.75, 0.30, 0.01}},
        {"sonnet[1m]", {3.00, 15.00, 3.75, 0.30, 0.01}},

        // Opus
        {"claude-opus-4", {15.00, 75.00, 18.75, 1.50, 0.01}},
        {"claude-opus-4-1", {15.00, 75.00, 18.75, 1.50, 0.01}},
        {"claude-opus-4-5", {5.00, 25.00, 6.25, 0.50, 0.01}},
        {"claude-opus-4-6", {5.00, 25.00, 6.25, 0.50, 0.01}},
        {"opus", {5.00, 25.00, 6.25, 0.50, 0.01}},
        {"opus[1m]", {15.00, 75.00, 18.75, 1.50, 0.01}},

        // Opus fast mode
        {"opus-fast", {30.00, 150.00, 37.50, 3.00, 0.01}},

        // GPT models (for OpenAI-compatible providers)
        {"gpt-4o", {2.50, 10.00, 0.0, 0.0, 0.0}},
        {"gpt-4o-mini", {0.15, 0.60, 0.0, 0.0, 0.0}},
        {"gpt-4-turbo", {10.00, 30.00, 0.0, 0.0, 0.0}},

        // Default unknown model pricing (tier 5/25)
        {"unknown", {5.00, 25.00, 6.25, 0.50, 0.01}},
    };
    return table;
}

String CostTracker::canonicalizeModelName(const String& model) {
    // Strip version suffixes
    if (model.find("claude-3-5-haiku") != String::npos) return "claude-3-5-haiku";
    if (model.find("claude-haiku-4-5") != String::npos) return "claude-haiku-4-5";
    if (model.find("claude-3-5-sonnet") != String::npos) return "claude-3-5-sonnet";
    if (model.find("claude-3-7-sonnet") != String::npos) return "claude-3-7-sonnet";
    if (model.find("claude-sonnet-4-5-20250929") != String::npos) return "claude-sonnet-4-5";
    if (model.find("claude-sonnet-4-5") != String::npos) return "claude-sonnet-4-5";
    if (model.find("claude-sonnet-4-6") != String::npos) return "claude-sonnet-4-6";
    if (model.find("claude-sonnet-4") != String::npos) return "claude-sonnet-4";
    if (model.find("claude-opus-4-6") != String::npos) return "claude-opus-4-6";
    if (model.find("claude-opus-4-5") != String::npos) return "claude-opus-4-5";
    if (model.find("claude-opus-4-1") != String::npos) return "claude-opus-4-1";
    if (model.find("claude-opus-4") != String::npos && model.find("opus-4-") == String::npos) return "claude-opus-4";

    // Short aliases
    if (model == "sonnet" || model == "sonnet[1m]") return model;
    if (model == "opus" || model == "opus[1m]") return model;
    if (model == "opus-fast") return model;

    return model;
}

ModelPricing CostTracker::getPricing(const String& model) {
    auto canonical = canonicalizeModelName(model);
    auto& table = pricingTable();
    auto it = table.find(canonical);
    if (it != table.end()) return it->second;

    // Try original name
    it = table.find(model);
    if (it != table.end()) return it->second;

    // Unknown model
    return table.at("unknown");
}

void CostTracker::registerPricing(const String& model, const ModelPricing& pricing) {
    pricingTable()[model] = pricing;
}

double CostTracker::calculateCost(const String& model, const Usage& usage) {
    return calculateCostWithCache(model,
        usage.promptTokens, usage.completionTokens, 0, 0, 0);
}

double CostTracker::calculateCostWithCache(const String& model,
    long inputTokens, long outputTokens,
    long cacheReadTokens, long cacheWriteTokens,
    long webSearchRequests)
{
    auto pricing = getPricing(model);
    return (static_cast<double>(inputTokens) / 1'000'000.0) * pricing.inputPerMtok
         + (static_cast<double>(outputTokens) / 1'000'000.0) * pricing.outputPerMtok
         + (static_cast<double>(cacheReadTokens) / 1'000'000.0) * pricing.cacheReadPerMtok
         + (static_cast<double>(cacheWriteTokens) / 1'000'000.0) * pricing.cacheWritePerMtok
         + static_cast<double>(webSearchRequests) * pricing.webSearchPerRequest;
}

void CostTracker::recordUsage(const String& model, const Usage& usage, double costUSD) {
    recordUsageWithCache(model, usage.promptTokens, usage.completionTokens,
        0, 0, 0, costUSD);
}

void CostTracker::recordUsageWithCache(const String& model,
    long inputTokens, long outputTokens,
    long cacheReadTokens, long cacheWriteTokens,
    long webSearchRequests, double costUSD)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto canonical = canonicalizeModelName(model);
    auto& usage = modelUsage_[canonical];
    usage.inputTokens += inputTokens;
    usage.outputTokens += outputTokens;
    usage.cacheReadTokens += cacheReadTokens;
    usage.cacheWriteTokens += cacheWriteTokens;
    usage.webSearchRequests += webSearchRequests;
    usage.costUSD += costUSD;

    totalCost_ += costUSD;

    if (canonical == "unknown") {
        hasUnknownModelCost_ = true;
    }
}

double CostTracker::getTotalCost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalCost_;
}

const std::unordered_map<String, ModelUsage>& CostTracker::getModelUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return modelUsage_;
}

ModelUsage CostTracker::getUsageForModel(const String& model) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto canonical = canonicalizeModelName(model);
    auto it = modelUsage_.find(canonical);
    return it != modelUsage_.end() ? it->second : ModelUsage{};
}

String CostTracker::formatCost(double costUSD) {
    if (costUSD >= 0.50) {
        char buf[32];
        snprintf(buf, sizeof(buf), "$%.2f", costUSD);
        return String(buf);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "$%.4f", costUSD);
    return String(buf);
}

String CostTracker::formatTotalCost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    String result = "Total cost: " + formatCost(totalCost_) + "\n";
    result += formatModelBreakdown();
    return result;
}

String CostTracker::formatModelBreakdown() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (modelUsage_.empty()) return "";

    String result = "Usage by model:\n";
    for (const auto& [model, usage] : modelUsage_) {
        result += "  " + model + ": ";
        result += std::to_string(usage.inputTokens) + " input, ";
        result += std::to_string(usage.outputTokens) + " output";
        if (usage.cacheReadTokens > 0) {
            result += ", " + std::to_string(usage.cacheReadTokens) + " cache read";
        }
        if (usage.cacheWriteTokens > 0) {
            result += ", " + std::to_string(usage.cacheWriteTokens) + " cache write";
        }
        result += " (" + formatCost(usage.costUSD) + ")\n";
    }
    return result;
}

void CostTracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    modelUsage_.clear();
    totalCost_ = 0.0;
    hasUnknownModelCost_ = false;
}

CostTracker::CacheSavings CostTracker::getCacheSavings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CacheSavings savings;

    for (const auto& [model, usage] : modelUsage_) {
        auto pricing = getPricing(model);
        savings.totalCacheReadTokens += usage.cacheReadTokens;
        savings.totalCacheWriteTokens += usage.cacheWriteTokens;
        savings.totalInputTokens += usage.inputTokens + usage.cacheReadTokens;

        // Cost saved: cache read is cheaper than full input price
        double fullInputCost = (static_cast<double>(usage.cacheReadTokens) / 1'000'000.0) * pricing.inputPerMtok;
        double cacheReadCost = (static_cast<double>(usage.cacheReadTokens) / 1'000'000.0) * pricing.cacheReadPerMtok;
        savings.cacheReadCostSaved += fullInputCost - cacheReadCost;

        // Cache write cost (paid upfront for future savings)
        savings.cacheWriteCost += (static_cast<double>(usage.cacheWriteTokens) / 1'000'000.0) * pricing.cacheWritePerMtok;
    }

    if (savings.totalInputTokens > 0) {
        savings.savingsPercent = (static_cast<double>(savings.totalCacheReadTokens) /
            static_cast<double>(savings.totalInputTokens)) * 100.0;
    }

    return savings;
}

String CostTracker::formatCacheSavings() const {
    auto savings = getCacheSavings();
    if (savings.totalInputTokens == 0) return "";

    String result = "Cache savings:\n";
    result += "  " + std::to_string(savings.totalCacheReadTokens) + " tokens served from cache ("
        + std::to_string(static_cast<int>(savings.savingsPercent)) + "% of input)\n";
    if (savings.cacheReadCostSaved > 0.001) {
        result += "  Saved " + formatCost(savings.cacheReadCostSaved) + " via prompt caching\n";
    }
    if (savings.cacheWriteCost > 0.001) {
        result += "  Cache write cost: " + formatCost(savings.cacheWriteCost) + "\n";
    }
    return result;
}

} // namespace claude
