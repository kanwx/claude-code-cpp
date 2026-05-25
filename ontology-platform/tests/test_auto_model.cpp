#include "TestUtils.hpp"
#include <ontology/AutoModel.hpp>
#include <ontology/Storage.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

static std::shared_ptr<HybridStorage> makeStorage() {
    return std::make_shared<HybridStorage>(nullptr, nullptr);
}

void test_foil_produces_nontrivial_rule() {
    TEST("FOIL algorithm produces non-trivial rule from examples");

    auto storage = makeStorage();
    AutoModelConfig config;
    auto llm = std::make_shared<LLMInterface>(config);
    RuleGenerator gen(llm, storage);

    auto* ts = storage->getTripleStore();
    ts->add({"alice", "http://example.org/isA", "Student"});
    ts->add({"alice", "http://example.org/attends", "Math101"});
    ts->add({"bob", "http://example.org/isA", "Student"});
    ts->add({"bob", "http://example.org/attends", "CS101"});

    std::vector<Triple> positive = {
        {"alice", "http://example.org/attends", "Math101"},
        {"bob", "http://example.org/attends", "CS101"}
    };
    std::vector<Triple> negative = {
        {"carol", "http://example.org/isA", "Professor"},
        {"carol", "http://example.org/teaches", "Math101"}
    };

    auto rules = gen.induceFromExamples(positive, negative);
    ASSERT_TRUE(!rules.empty());

    auto& rule = rules[0];
    ASSERT_TRUE(!rule.body.empty());
    ASSERT_TRUE(!rule.head.empty());

    // Head should NOT be identical to body (the old bug)
    bool isIdentity = (rule.body.size() == rule.head.size());
    if (isIdentity) {
        for (size_t i = 0; i < rule.body.size(); ++i) {
            if (rule.body[i].propertyId != rule.head[i].propertyId ||
                rule.body[i].argument1 != rule.head[i].argument1 ||
                rule.body[i].argument2 != rule.head[i].argument2) {
                isIdentity = false;
                break;
            }
        }
    }
    ASSERT_TRUE(!isIdentity);
    ASSERT_TRUE(rule.confidence > 0.0f);

    PASS();
}

int main() {
    test_foil_produces_nontrivial_rule();
    std::cout << "\nAutoModel tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
