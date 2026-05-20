#include <catch2/catch_test_macros.hpp>
#include <claude/core/compact/AutoCompact.hpp>
#include <claude/api/ApiClient.hpp>
#include <memory>

using namespace claude;
using namespace claude::compact;

// Mock ApiClient that always fails
class FailingApiClient : public ApiClient {
public:
    std::expected<Json, String> call(const Json&, const Json&) override {
        return std::unexpected("mock failure");
    }
    void stream(const Json&, const Json&, std::function<void(const Json&)>) override {}
    void setApiKey(const String&) override {}
    void setBaseUrl(const String&) override {}
    void setModel(const String& m) override { model_ = m; }
    void setMaxTokens(int) override {}
    void setTemperature(double) override {}
    String getProviderName() const override { return "mock"; }
    String getModelName() const override { return model_; }
private:
    String model_ = "mock";
};

TEST_CASE("AutoCompact circuit breaker: no failures initially", "[compact]") {
    auto client = std::make_unique<FailingApiClient>();
    AutoCompact ac(*client, 100000);
    REQUIRE(ac.getConsecutiveFailures() == 0);
    REQUIRE_FALSE(ac.isCircuitOpen());
}

TEST_CASE("AutoCompact circuit breaker: opens after MAX_CONSECUTIVE_FAILURES", "[compact]") {
    auto client = std::make_unique<FailingApiClient>();
    AutoCompact ac(*client, 100000);

    std::vector<Message> history;
    history.push_back(Message::system("system"));
    for (int i = 0; i < 20; ++i) {
        history.push_back(Message::user("msg " + std::to_string(i)));
        history.push_back(Message::assistant("resp " + std::to_string(i)));
    }

    for (int i = 0; i < AutoCompact::MAX_CONSECUTIVE_FAILURES; ++i) {
        ac.compact(history);
    }
    REQUIRE(ac.getConsecutiveFailures() == AutoCompact::MAX_CONSECUTIVE_FAILURES);
    REQUIRE(ac.isCircuitOpen());
    REQUIRE_FALSE(ac.shouldTrigger(99999));
}

TEST_CASE("AutoCompact circuit breaker: resets on success", "[compact]") {
    auto client = std::make_unique<FailingApiClient>();
    AutoCompact ac(*client, 100000);
    ac.recordFailure();
    ac.recordFailure();
    REQUIRE(ac.getConsecutiveFailures() == 2);
    REQUIRE_FALSE(ac.isCircuitOpen());
    ac.recordSuccess();
    REQUIRE(ac.getConsecutiveFailures() == 0);
    REQUIRE_FALSE(ac.isCircuitOpen());
}
