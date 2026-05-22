#include "TestUtils.hpp"
#include <ontology/Inference.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// Helper: create an in-memory HybridStorage with classes and individuals
static std::shared_ptr<HybridStorage> makeTestStorage() {
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    return storage;
}

// Helper: add a Class with superClasses to storage
static void addClass(HybridStorage& storage, const String& id, const String& name,
                     const std::vector<String>& supers = {},
                     const std::vector<String>& disjoints = {}) {
    Class cls;
    cls.id = id;
    cls.name = name;
    cls.superClasses = supers;
    cls.disjointClasses = disjoints;
    storage.addClass(cls);
}

// Helper: add an Individual to storage
static void addIndividual(HybridStorage& storage, const String& id, const String& name,
                          const String& classId) {
    Individual ind;
    ind.id = id;
    ind.name = name;
    ind.classId = classId;
    storage.addIndividual(ind);
}

// ============================================================================
// Test: Type inference through class hierarchy
// ============================================================================
void testTypeInference() {
    TEST("SymbolicReasoner type inference");

    auto storage = makeTestStorage();

    // Person -> Employee -> Manager hierarchy
    addClass(*storage, "Person", "Person");
    addClass(*storage, "Employee", "Employee", {"Person"});
    addClass(*storage, "Manager", "Manager", {"Employee"});

    // Individual is a Manager
    addIndividual(*storage, "alice", "Alice", "Manager");

    SymbolicReasoner reasoner(storage);

    auto types = reasoner.getTypes("alice");

    // Should include Manager (direct), Employee, and Person (inferred)
    ASSERT_TRUE(std::find(types.begin(), types.end(), "Manager") != types.end());
    ASSERT_TRUE(std::find(types.begin(), types.end(), "Employee") != types.end());
    ASSERT_TRUE(std::find(types.begin(), types.end(), "Person") != types.end());
    ASSERT_EQ(types.size(), 3u);

    PASS();
}

// ============================================================================
// Test: Attribute inheritance - subclasses inherit from superclasses
// ============================================================================
void testAttributeInheritance() {
    TEST("SymbolicReasoner attribute inheritance");

    auto storage = makeTestStorage();

    // Person has a "hasName" property
    addClass(*storage, "Person", "Person");
    addClass(*storage, "Employee", "Employee", {"Person"});

    // Add property triple for Person
    storage->addTriple(makeTriple("Person", "hasProperty", "hasName"));

    addIndividual(*storage, "bob", "Bob", "Employee");

    SymbolicReasoner reasoner(storage);

    // Employee should be subclass of Person
    ASSERT_TRUE(reasoner.isSubClassOf("Employee", "Person"));
    ASSERT_TRUE(!reasoner.isSubClassOf("Person", "Employee"));

    // isInstanceOf should traverse hierarchy
    ASSERT_TRUE(reasoner.isInstanceOf("bob", "Person"));
    ASSERT_TRUE(reasoner.isInstanceOf("bob", "Employee"));

    PASS();
}

// ============================================================================
// Test: Rule-based relation inference
// ============================================================================
void testRelationInference() {
    TEST("SymbolicReasoner relation inference");

    auto storage = makeTestStorage();

    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    addIndividual(*storage, "bob", "Bob", "Person");

    // Add manages(Alice, Bob) triple
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    // Add rule: manager(X,Y) => canApprove(X,Y)
    Axiom rule;
    rule.id = "mgr_approve";
    rule.type = Axiom::Type::CustomSWRL;
    rule.premise = "manages(X, Y)";
    rule.conclusion = "canApprove(X, Y)";
    rule.confidence = 1.0f;

    SymbolicReasoner reasoner(storage);
    reasoner.addAxiom(rule);

    auto inferred = reasoner.applyRules("alice", 3);
    ASSERT_TRUE(!inferred.empty());

    // Should infer canApprove(alice, Y)
    bool found = false;
    for (const auto& t : inferred) {
        if (t.predicate == "canApprove") {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    PASS();
}

// ============================================================================
// Test: Consistency check - disjoint classes violation
// ============================================================================
void testConsistencyCheck() {
    TEST("SymbolicReasoner consistency check");

    auto storage = makeTestStorage();

    // Person and Organization are disjoint
    addClass(*storage, "Person", "Person", {}, {"Organization"});
    addClass(*storage, "Organization", "Organization", {}, {"Person"});

    // Individual belongs to both disjoint classes - this is tricky since
    // an individual has only one classId. We add two individuals both
    // pointing to the same name to test the check.
    // Actually, the check uses getTypes which traverses superClasses.
    // Let's create a class that has both Person and Organization as supers.
    addClass(*storage, "HybridEntity", "HybridEntity", {"Person", "Organization"});
    addIndividual(*storage, "entity1", "Entity1", "HybridEntity");

    SymbolicReasoner reasoner(storage);

    auto conflicts = reasoner.checkConsistency();
    ASSERT_TRUE(!conflicts.empty());

    bool foundDisjoint = false;
    for (const auto& c : conflicts) {
        if (c.description.find("disjoint") != String::npos) {
            foundDisjoint = true;
            break;
        }
    }
    ASSERT_TRUE(foundDisjoint);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SymbolicReasoner Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testTypeInference();
    testAttributeInheritance();
    testRelationInference();
    testConsistencyCheck();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
