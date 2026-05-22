#include "TestUtils.hpp"
#include <ontology/Swrl.hpp>
#include <ontology/SwrlBackwardChainer.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Helpers
// ============================================================================

static SwrlAtom classAtom(const String& classId, const String& arg) {
    SwrlAtom a;
    a.type = SwrlAtomType::ClassAtom;
    a.classId = classId;
    a.classArgument = arg;
    return a;
}

static SwrlAtom objPropAtom(const String& propId, const String& arg1, const String& arg2) {
    SwrlAtom a;
    a.type = SwrlAtomType::ObjectPropertyAtom;
    a.propertyId = propId;
    a.argument1 = arg1;
    a.argument2 = arg2;
    return a;
}

static Individual makeInd(const String& id, const String& name, const String& classId) {
    Individual ind;
    ind.id = id;
    ind.name = name;
    ind.classId = classId;
    return ind;
}

static SwrlRule makeRule(const String& id, const String& name,
                         const std::vector<SwrlAtom>& body,
                         const std::vector<SwrlAtom>& head) {
    SwrlRule r;
    r.id = id;
    r.name = name;
    r.body = body;
    r.head = head;
    return r;
}

// Helper: check if a binding vector contains a mapping key->value
static bool hasBinding(const Bindings& bindings, const String& key, const String& value) {
    for (const auto& b : bindings) {
        auto it = b.find(key);
        if (it != b.end() && it->second == value) return true;
    }
    return false;
}

// ============================================================================
// Test 1: Prove simple goal from facts
// Storage: alice is Manager
// Goal: Manager(?x)
// Expect: binding {x -> alice}
// ============================================================================
void test_prove_simple_goal_from_facts() {
    TEST("Prove simple goal from facts");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));

    SwrlBackwardChainer chainer(storage);
    // No rules needed -- fact is in storage

    SwrlAtom goal = classAtom("Manager", "?x");
    auto bindings = chainer.prove({goal});

    ASSERT_EQ(bindings.size(), 1u);
    ASSERT_TRUE(hasBinding(bindings, "x", "alice"));

    PASS();
}

// ============================================================================
// Test 2: Prove goal requiring single rule application
// Rule: Employee(?x) -> Manager(?x)
// Storage: alice is Employee
// Goal: Manager(?x)
// Expect: binding {x -> alice} via rule
// ============================================================================
void test_prove_goal_with_rule() {
    TEST("Prove goal requiring rule application");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Employee"));

    std::vector<SwrlRule> rules;
    rules.push_back(makeRule("r1", "emp_to_mgr",
        { classAtom("Employee", "?x") },
        { classAtom("Manager", "?x") }));

    SwrlBackwardChainer chainer(storage);
    chainer.setRules(rules);

    SwrlAtom goal = classAtom("Manager", "?x");
    auto bindings = chainer.prove({goal});

    ASSERT_EQ(bindings.size(), 1u);
    ASSERT_TRUE(hasBinding(bindings, "x", "alice"));

    PASS();
}

// ============================================================================
// Test 3: Recursive sub-goals (two-step chain)
// Rule1: Employee(?x) -> Manager(?x)
// Rule2: Person(?x) -> Employee(?x)
// Storage: alice is Person
// Goal: Manager(?x)
// Expect: binding {x -> alice} via two rules
// ============================================================================
void test_recursive_subgoals() {
    TEST("Recursive sub-goals (two-step chain)");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    std::vector<SwrlRule> rules;
    rules.push_back(makeRule("r1", "emp_to_mgr",
        { classAtom("Employee", "?x") },
        { classAtom("Manager", "?x") }));
    rules.push_back(makeRule("r2", "person_to_emp",
        { classAtom("Person", "?x") },
        { classAtom("Employee", "?x") }));

    SwrlBackwardChainer chainer(storage);
    chainer.setRules(rules);

    SwrlAtom goal = classAtom("Manager", "?x");
    auto bindings = chainer.prove({goal});

    ASSERT_EQ(bindings.size(), 1u);
    ASSERT_TRUE(hasBinding(bindings, "x", "alice"));

    PASS();
}

// ============================================================================
// Test 4: Proof tree structure
// Rule: Employee(?x) -> Manager(?x)
// Storage: alice is Employee
// Goal: Manager(?x)
// Verify: proven=true, matchedRule non-null, subGoals present
// ============================================================================
void test_proof_tree_structure() {
    TEST("Proof tree structure");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Employee"));

    std::vector<SwrlRule> rules;
    rules.push_back(makeRule("r1", "emp_to_mgr",
        { classAtom("Employee", "?x") },
        { classAtom("Manager", "?x") }));

    SwrlBackwardChainer chainer(storage);
    chainer.setRules(rules);

    SwrlAtom goal = classAtom("Manager", "?x");
    auto tree = chainer.buildProofTree({goal});

    ASSERT_TRUE(tree.proven);
    // Root is a conjunction wrapper; the actual rule match is in subGoals
    ASSERT_TRUE(!tree.subGoals.empty());

    // The first sub-goal is Manager(?x) proven via rule r1
    const auto& managerNode = tree.subGoals[0];
    ASSERT_TRUE(managerNode.proven);
    ASSERT_TRUE(managerNode.matchedRule != nullptr);
    ASSERT_EQ(managerNode.matchedRule->id, "r1");

    // The rule's sub-goal should be Employee(?x) proven from facts
    bool hasEmpSubGoal = false;
    for (const auto& sg : managerNode.subGoals) {
        if (sg.goal.type == SwrlAtomType::ClassAtom &&
            sg.goal.classId == "Employee") {
            hasEmpSubGoal = true;
            ASSERT_TRUE(sg.proven);
            // Fact proven -- matchedRule is null
            ASSERT_TRUE(sg.matchedRule == nullptr);
        }
    }
    ASSERT_TRUE(hasEmpSubGoal);

    PASS();
}

// ============================================================================
// Test 5: Multiple solutions
// Storage: alice and bob both are Manager
// Goal: Manager(?x)
// Expect: 2 bindings with x=alice and x=bob
// ============================================================================
void test_multiple_solutions() {
    TEST("Multiple solutions from multiple individuals");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));
    storage->addIndividual(makeInd("bob", "Bob", "Manager"));

    SwrlBackwardChainer chainer(storage);

    SwrlAtom goal = classAtom("Manager", "?x");
    auto bindings = chainer.prove({goal});

    ASSERT_EQ(bindings.size(), 2u);
    ASSERT_TRUE(hasBinding(bindings, "x", "alice"));
    ASSERT_TRUE(hasBinding(bindings, "x", "bob"));

    PASS();
}

// ============================================================================
// Test 6: Depth limit prevents deep recursion
// Rule1: Employee(?x) -> Manager(?x)
// Rule2: Person(?x) -> Employee(?x)
// Storage: alice is Person
// Goal: Manager(?x) with maxDepth=1
// Expect: empty bindings (cannot traverse two rule steps)
// ============================================================================
void test_depth_limit() {
    TEST("Depth limit prevents deep recursion");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    std::vector<SwrlRule> rules;
    rules.push_back(makeRule("r1", "emp_to_mgr",
        { classAtom("Employee", "?x") },
        { classAtom("Manager", "?x") }));
    rules.push_back(makeRule("r2", "person_to_emp",
        { classAtom("Person", "?x") },
        { classAtom("Employee", "?x") }));

    SwrlBackwardChainer chainer(storage);
    chainer.setRules(rules);

    SwrlAtom goal = classAtom("Manager", "?x");
    auto bindings = chainer.prove({goal}, 1);

    ASSERT_EQ(bindings.size(), 0u);

    PASS();
}

// ============================================================================
// Test 7: Unprovable goal
// Goal: NonExistentClass(?x)
// Storage has no such class/individual
// Expect: empty bindings
// ============================================================================
void test_unprovable_goal() {
    TEST("Unprovable goal returns empty bindings");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));

    SwrlBackwardChainer chainer(storage);

    SwrlAtom goal = classAtom("NonExistentClass", "?x");
    auto bindings = chainer.prove({goal});

    ASSERT_EQ(bindings.size(), 0u);

    PASS();
}

// ============================================================================
// Test 8: ObjectPropertyAtom goal with variable bindings
// Storage: (alice, manages, bob) triple
// Goal: manages(?x, ?y)
// Expect: binding {x -> alice, y -> bob}
// ============================================================================
void test_object_property_goal() {
    TEST("ObjectProperty goal with variable bindings");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));
    storage->addIndividual(makeInd("bob", "Bob", "Person"));
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    SwrlBackwardChainer chainer(storage);

    SwrlAtom goal = objPropAtom("manages", "?x", "?y");
    auto bindings = chainer.prove({goal});

    ASSERT_EQ(bindings.size(), 1u);
    ASSERT_TRUE(hasBinding(bindings, "x", "alice"));
    ASSERT_TRUE(hasBinding(bindings, "y", "bob"));

    PASS();
}

// ============================================================================
// Test 9: Conjunction goal (two atoms)
// Storage: alice is Manager, (alice, manages, bob)
// Goal: Manager(?x) AND manages(?x, ?y)
// Expect: binding {x -> alice, y -> bob}
// ============================================================================
void test_conjunction_goal() {
    TEST("Conjunction goal with two atoms");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));
    storage->addIndividual(makeInd("bob", "Bob", "Person"));
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    SwrlBackwardChainer chainer(storage);

    auto bindings = chainer.prove({
        classAtom("Manager", "?x"),
        objPropAtom("manages", "?x", "?y")
    });

    ASSERT_EQ(bindings.size(), 1u);
    ASSERT_TRUE(hasBinding(bindings, "x", "alice"));
    ASSERT_TRUE(hasBinding(bindings, "y", "bob"));

    PASS();
}

// ============================================================================
// Test 10: Proof tree for fact-only goal has matchedRule=nullptr
// Storage: alice is Manager
// Goal: Manager(?x)
// Expect: proven, no matchedRule (direct fact), no subGoals
// ============================================================================
void test_proof_tree_fact_only() {
    TEST("Proof tree for fact-only goal");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));

    SwrlBackwardChainer chainer(storage);

    auto tree = chainer.buildProofTree({ classAtom("Manager", "?x") });

    ASSERT_TRUE(tree.proven);
    // Direct fact -- no rule used
    ASSERT_TRUE(tree.matchedRule == nullptr);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SWRL Backward Chaining Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    test_prove_simple_goal_from_facts();
    test_prove_goal_with_rule();
    test_recursive_subgoals();
    test_proof_tree_structure();
    test_multiple_solutions();
    test_depth_limit();
    test_unprovable_goal();
    test_object_property_goal();
    test_conjunction_goal();
    test_proof_tree_fact_only();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
