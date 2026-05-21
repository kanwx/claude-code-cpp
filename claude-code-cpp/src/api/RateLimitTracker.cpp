#include <claude/api/RateLimitTracker.hpp>
#include <sstream>
#include <iomanip>

namespace claude {

// ========== RateLimitInfo ==========

RateLimitInfo RateLimitInfo::fromHeaders(const std::map<String, String>& headers) {
    RateLimitInfo info;
    info.lastUpdated = std::chrono::system_clock::now();

    // 解析请求限制
    auto it = headers.find("anthropic-ratelimit-unified-limit");
    if (it != headers.end()) {
        try { info.requestsLimit = std::stoi(it->second); } catch (...) {}
    }

    it = headers.find("anthropic-ratelimit-unified-remaining");
    if (it != headers.end()) {
        try { info.requestsRemaining = std::stoi(it->second); } catch (...) {}
    }

    it = headers.find("anthropic-ratelimit-unified-reset");
    if (it != headers.end()) {
        // ISO 8601 日期格式
        try {
            std::istringstream iss(it->second);
            std::tm tm = {};
            iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (!iss.fail()) {
                info.requestsResetTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
        } catch (...) {}
    }

    // 解析 Token 限制
    it = headers.find("anthropic-ratelimit-tokens-limit");
    if (it != headers.end()) {
        try { info.tokensLimit = std::stoi(it->second); } catch (...) {}
    }

    it = headers.find("anthropic-ratelimit-tokens-remaining");
    if (it != headers.end()) {
        try { info.tokensRemaining = std::stoi(it->second); } catch (...) {}
    }

    it = headers.find("anthropic-ratelimit-tokens-reset");
    if (it != headers.end()) {
        try {
            std::istringstream iss(it->second);
            std::tm tm = {};
            iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (!iss.fail()) {
                info.tokensResetTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
        } catch (...) {}
    }

    // 解析 retry-after
    it = headers.find("retry-after");
    if (it != headers.end()) {
        try { info.retryAfter = std::stoi(it->second); } catch (...) {}
    }

    // Detect overage: remaining < 0 means we've exceeded the limit
    info.isOverage = (info.requestsRemaining < 0 || info.tokensRemaining < 0);

    // Detect tier based on request limit
    if (info.requestsLimit > 4000) {
        info.tierName = "tier_2+";
    } else if (info.requestsLimit > 1000) {
        info.tierName = "tier_1";
    } else if (info.requestsLimit > 0) {
        info.tierName = "free";
    }

    return info;
}

String RateLimitInfo::warningMessage() const {
    std::ostringstream oss;

    if (isRequestLimitExceeded()) {
        oss << "Request limit exceeded (" << requestsRemaining << "/" << requestsLimit
            << "). Please wait before making more requests.";
        return oss.str();
    }

    if (isTokenLimitExceeded()) {
        oss << "Token limit exceeded (" << tokensRemaining << "/" << tokensLimit
            << "). Please wait before making more requests.";
        return oss.str();
    }

    if (isRequestLimitLow()) {
        int pct = requestsLimit > 0 ? (requestsRemaining * 100 / requestsLimit) : 0;
        oss << "Warning: Request limit low (" << pct << "% remaining: "
            << requestsRemaining << "/" << requestsLimit << ")";
        return oss.str();
    }

    if (isTokenLimitLow()) {
        int pct = tokensLimit > 0 ? (tokensRemaining * 100 / tokensLimit) : 0;
        oss << "Warning: Token limit low (" << pct << "% remaining: "
            << tokensRemaining << "/" << tokensLimit << ")";
        return oss.str();
    }

    return "";
}

// ========== RateLimitTracker ==========

void RateLimitTracker::updateFromHeaders(const std::map<String, String>& headers) {
    std::lock_guard lock(mutex_);
    info_ = RateLimitInfo::fromHeaders(headers);

    // 如果有速率限制信息，记录日志
    if (info_.requestsLimit > 0) {
        spdlog::debug("Rate limit: {}/{} requests, {}/{} tokens",
            info_.requestsRemaining, info_.requestsLimit,
            info_.tokensRemaining, info_.tokensLimit);
    }
}

void RateLimitTracker::updateFromHttpHeaders(const httplib::Headers& headers) {
    std::map<String, String> headerMap;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        headerMap[it->first] = it->second;
    }
    updateFromHeaders(headerMap);
}

void RateLimitTracker::recordRateLimitError(int statusCode, const String& body) {
    std::lock_guard lock(mutex_);
    consecutiveErrors_++;
    totalErrors_++;
    info_.isOverloaded = (statusCode == 529 || statusCode == 429);

    if (statusCode == 429) {
        spdlog::warn("Rate limited (429): Too many requests. Consecutive errors: {}", consecutiveErrors_);
    } else if (statusCode == 529) {
        spdlog::warn("Overloaded (529): API is overloaded. Consecutive errors: {}", consecutiveErrors_);
    } else {
        spdlog::warn("API error ({}). Consecutive errors: {}", statusCode, consecutiveErrors_);
    }
}

void RateLimitTracker::recordSuccess() {
    std::lock_guard lock(mutex_);
    consecutiveErrors_ = 0;
    totalRequests_++;
    info_.isOverloaded = false;
}

bool RateLimitTracker::shouldShowWarning() const {
    std::lock_guard lock(mutex_);

    // 总是显示超过限制的警告
    if (info_.isRequestLimitExceeded() || info_.isTokenLimitExceeded()) {
        return true;
    }

    // 冷却时间内不重复显示
    auto now = std::chrono::steady_clock::now();
    if (now - lastWarningShown_ < WARNING_COOLDOWN) {
        return false;
    }

    // 显示低限制警告
    if (info_.isRequestLimitLow() || info_.isTokenLimitLow()) {
        return true;
    }

    return false;
}

String RateLimitTracker::usageSummary() const {
    std::ostringstream oss;
    oss << "=== API Usage ===\n\n";

    if (info_.requestsLimit > 0) {
        int pct = info_.requestsLimit > 0 ? (100 - info_.requestsRemaining * 100 / info_.requestsLimit) : 0;
        oss << "Requests: " << (info_.requestsLimit - info_.requestsRemaining)
            << "/" << info_.requestsLimit << " (" << pct << "% used)\n";
    }

    if (info_.tokensLimit > 0) {
        int pct = info_.tokensLimit > 0 ? (100 - info_.tokensRemaining * 100 / info_.tokensLimit) : 0;
        oss << "Tokens:   " << (info_.tokensLimit - info_.tokensRemaining)
            << "/" << info_.tokensLimit << " (" << pct << "% used)\n";
    }

    oss << "\nTotal requests: " << totalRequests_ << "\n";
    oss << "Total errors:   " << totalErrors_ << "\n";

    if (consecutiveErrors_ > 0) {
        oss << "Consecutive errors: " << consecutiveErrors_ << "\n";
    }

    String warning = info_.warningMessage();
    if (!warning.empty()) {
        oss << "\n" << warning << "\n";
    }

    return oss.str();
}

String RateLimitTracker::statusMessage() const {
    std::lock_guard lock(mutex_);

    // Overage takes highest priority
    if (info_.isOverage) {
        String msg = "Rate limit OVERAGE — ";
        if (info_.requestsRemaining < 0) {
            msg += "requests: " + std::to_string(info_.requestsRemaining) + "/" + std::to_string(info_.requestsLimit);
        }
        if (info_.tokensRemaining < 0) {
            if (info_.requestsRemaining < 0) msg += ", ";
            msg += "tokens: " + std::to_string(info_.tokensRemaining) + "/" + std::to_string(info_.tokensLimit);
        }
        String resets = formatResetTime(info_.requestsResetTime);
        if (resets != "unknown") {
            msg += " (" + resets + ")";
        }
        return msg;
    }

    if (info_.requestsRemaining >= 0 && info_.requestsLimit > 0) {
        double usagePercent = 100.0 * (1.0 - static_cast<double>(info_.requestsRemaining) / info_.requestsLimit);
        if (usagePercent >= 95.0) {
            String msg = "Rate limit nearly exhausted (" + std::to_string(info_.requestsRemaining) +
                   "/" + std::to_string(info_.requestsLimit) + " requests remaining)";
            String resets = formatResetTime(info_.requestsResetTime);
            if (resets != "unknown") {
                msg += " (" + resets + ")";
            }
            return msg;
        }
        if (usagePercent >= 80.0) {
            return "Rate limit usage high (" +
                   std::to_string(static_cast<int>(usagePercent)) + "%)";
        }
    }

    if (info_.isOverloaded) {
        return "API is currently overloaded. Retries may be delayed.";
    }

    return "";
}

bool RateLimitTracker::isOverage() const {
    std::lock_guard lock(mutex_);
    return info_.isOverage;
}

const String& RateLimitTracker::tierName() const {
    std::lock_guard lock(mutex_);
    return info_.tierName;
}

String RateLimitTracker::requestLimitResetsAt() const {
    std::lock_guard lock(mutex_);
    return formatResetTime(info_.requestsResetTime);
}

String RateLimitTracker::tokenLimitResetsAt() const {
    std::lock_guard lock(mutex_);
    return formatResetTime(info_.tokensResetTime);
}

String RateLimitTracker::formatResetTime(const std::chrono::system_clock::time_point& tp) {
    if (tp == std::chrono::system_clock::time_point{}) {
        return "unknown";
    }
    auto timeT = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&timeT);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return String("resets at ") + buf;
}

} // namespace claude
