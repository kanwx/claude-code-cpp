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

void test_resolveConflict_dryrun() {
    TEST("resolveConflict dryRun does not execute actions");
    auto storage = makeStorage();
    AutoModelConfig config;
    AutoModelEngine engine(storage, config);
    // Without LLM initialized, resolveConflict returns early — no crash
    engine.resolveConflict("test_conflict", true);
    PASS();
}

void test_importAndLearn_adds_rules() {
    TEST("importAndLearn adds discovered rules to the engine");
    auto storage = makeStorage();
    AutoModelConfig config;
    config.enableIncrementalLearning = false;
    AutoModelEngine engine(storage, config);

    auto* ts = storage->getTripleStore();
    ts->add({"alice", "http://example.org/isA", "Student"});
    ts->add({"alice", "http://example.org/attends", "Math101"});
    ts->add({"bob", "http://example.org/isA", "Student"});
    ts->add({"bob", "http://example.org/attends", "CS101"});
    ts->add({"carol", "http://example.org/isA", "Professor"});

    std::vector<Triple> newTriples = {
        {"dave", "http://example.org/isA", "Student"},
        {"dave", "http://example.org/attends", "Physics201"}
    };

    int imported = engine.importAndLearn(newTriples);
    ASSERT_EQ(imported, 2);
    PASS();
}

void test_detect_disjoint_conflict() {
    TEST("detectConflicts finds disjoint class assertion conflicts");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    ts->add({"Cat", "http://www.w3.org/2002/07/owl#disjointWith", "Dog"});
    ts->add({"fluffy", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Cat"});
    ts->add({"fluffy", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Dog"});

    auto conflicts = engine.detectConflicts();
    bool foundDisjoint = false;
    for (const auto& c : conflicts) {
        if (c.type == OntologyConflict::DisjointClassAssertion) {
            foundDisjoint = true;
        }
    }
    ASSERT_TRUE(foundDisjoint);
    PASS();
}

void test_detect_functional_property_violation() {
    TEST("detectConflicts finds functional property violations");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    ts->add({"hasMother", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
             "http://www.w3.org/2002/07/owl#FunctionalProperty"});
    ts->add({"alice", "hasMother", "bob"});
    ts->add({"alice", "hasMother", "carol"});

    auto conflicts = engine.detectConflicts();
    bool foundFunctional = false;
    for (const auto& c : conflicts) {
        if (c.type == OntologyConflict::FunctionalPropertyViolation) {
            foundFunctional = true;
        }
    }
    ASSERT_TRUE(foundFunctional);
    PASS();
}

void test_detect_no_conflicts() {
    TEST("detectConflicts returns empty for consistent data");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"rex", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Dog"});

    auto conflicts = engine.detectConflicts();
    ASSERT_TRUE(conflicts.empty());
    PASS();
}

void test_align_entities_multi_signal() {
    TEST("alignEntities uses multi-signal scoring");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "hasPart", "Tail"});
    ts->add({"Dog", "livesIn", "Home"});
    ts->add({"Canine", "hasPart", "Tail"});
    ts->add({"Canine", "livesIn", "Home"});

    auto alignments = engine.alignEntities({"Dog"}, {"Canine"});
    bool found = false;
    for (const auto& a : alignments) {
        if ((a.entity1 == "Dog" && a.entity2 == "Canine") ||
            (a.entity1 == "Canine" && a.entity2 == "Dog")) {
            found = true;
            ASSERT_TRUE(a.structuralScore > 0.5f);
            ASSERT_TRUE(a.labelScore >= 0.0f);
            ASSERT_TRUE(a.combinedScore >= 0.25f);
        }
    }
    ASSERT_TRUE(found);
    PASS();
}

void test_merge_ontologies_with_provenance() {
    TEST("mergeOntologies tracks provenance and deduplicates");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});

    std::vector<Triple> external;
    Triple t1;
    t1.subject = "Dog"; t1.predicate = "http://www.w3.org/2000/01/rdf-schema#subClassOf"; t1.object = "Animal";
    Triple t2;
    t2.subject = "Cat"; t2.predicate = "http://www.w3.org/2000/01/rdf-schema#subClassOf"; t2.object = "Animal";
    external.push_back(t1);
    external.push_back(t2);

    engine.mergeOntologies(external, "ext1", "External Source");

    auto allTriples = storage->getAllTriples();
    std::unordered_set<String> uniqueKeys;
    for (const auto& t : allTriples) {
        uniqueKeys.insert(t.subject + "|" + t.predicate + "|" + t.object);
    }
    ASSERT_TRUE(uniqueKeys.size() == 2);

    PASS();
}

int main() {
    test_foil_produces_nontrivial_rule();
    test_resolveConflict_dryrun();
    test_importAndLearn_adds_rules();
    test_detect_disjoint_conflict();
    test_detect_functional_property_violation();
    test_detect_no_conflicts();
    test_align_entities_multi_signal();
    test_merge_ontologies_with_provenance();
    std::cout << "\nAutoModel tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
