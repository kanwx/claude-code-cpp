#include "TestUtils.hpp"
#include <ontology/Swrl.hpp>

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

static SwrlAtom dataPropAtom(const String& propId, const String& arg1, const String& arg2) {
    SwrlAtom a;
    a.type = SwrlAtomType::DataPropertyAtom;
    a.propertyId = propId;
    a.argument1 = arg1;
    a.argument2 = arg2;
    return a;
}

static SwrlAtom builtInAtom(const String& name, const std::vector<String>& args) {
    SwrlAtom a;
    a.type = SwrlAtomType::BuiltInAtom;
    a.builtInName = name;
    a.builtInArgs = args;
    return a;
}

static Individual makeInd(const String& id, const String& name, const String& classId) {
    Individual ind;
    ind.id = id;
    ind.name = name;
    ind.classId = classId;
    return ind;
}

// ============================================================================
// Test 1: Simple rule triggers new fact
// Person(?x) -> worksAt(?x, "Acme")
// Setup: alice is Person
// Expect: (alice, worksAt, Acme)
// ============================================================================
void test_simple_rule_triggers_new_fact() {
    TEST("Simple rule triggers new fact");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    SwrlRule rule;
    rule.id = "r1";
    rule.name = "person_works";
    rule.body = { classAtom("Person", "?x") };
    rule.head = { objPropAtom("worksAt", "?x", "Acme") };

    SwrlEngine engine(storage);
    engine.addRule(rule);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 1u);
    ASSERT_EQ(inferred[0].subject, "alice");
    ASSERT_EQ(inferred[0].predicate, "worksAt");
    ASSERT_EQ(inferred[0].object, "Acme");

    PASS();
}

// ============================================================================
// Test 2: Chain reasoning (A -> B -> C)
// Rule1: Person(?x) -> worksAt(?x, "Acme")
// Rule2: worksAt(?x, "Acme") -> employed(?x, "Yes")
// Setup: alice is Person
// Expect: (alice, worksAt, Acme) and (alice, employed, Yes)
// ============================================================================
void test_chain_reasoning() {
    TEST("Chain reasoning A->B->C");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    SwrlRule rule1;
    rule1.id = "r1";
    rule1.name = "person_works";
    rule1.body = { classAtom("Person", "?x") };
    rule1.head = { objPropAtom("worksAt", "?x", "Acme") };

    SwrlRule rule2;
    rule2.id = "r2";
    rule2.name = "works_employed";
    rule2.body = { objPropAtom("worksAt", "?x", "Acme") };
    rule2.head = { objPropAtom("employed", "?x", "Yes") };

    SwrlEngine engine(storage);
    engine.addRule(rule1);
    engine.addRule(rule2);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 2u);

    bool foundWorksAt = false, foundEmployed = false;
    for (const auto& t : inferred) {
        if (t.predicate == "worksAt" && t.subject == "alice" && t.object == "Acme")
            foundWorksAt = true;
        if (t.predicate == "employed" && t.subject == "alice" && t.object == "Yes")
            foundEmployed = true;
    }
    ASSERT_TRUE(foundWorksAt);
    ASSERT_TRUE(foundEmployed);

    PASS();
}

// ============================================================================
// Test 3: Variable binding propagation
// manages(?x, ?y) ^ Person(?y) -> supervisedBy(?y, ?x)
// Setup: alice manages bob, bob is Person
// Expect: (bob, supervisedBy, alice)
// ============================================================================
void test_variable_binding_propagation() {
    TEST("Variable binding propagation");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));
    storage->addIndividual(makeInd("bob", "Bob", "Person"));
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    SwrlRule rule;
    rule.id = "r1";
    rule.name = "managed_is_supervised";
    rule.body = {
        objPropAtom("manages", "?x", "?y"),
        classAtom("Person", "?y")
    };
    rule.head = { objPropAtom("supervisedBy", "?y", "?x") };

    SwrlEngine engine(storage);
    engine.addRule(rule);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 1u);
    ASSERT_EQ(inferred[0].subject, "bob");
    ASSERT_EQ(inferred[0].predicate, "supervisedBy");
    ASSERT_EQ(inferred[0].object, "alice");

    PASS();
}

// ============================================================================
// Test 4: Built-in function swrlb:equal
// Person(?x) ^ swrlb:equal(?x, "alice") -> specialRole(?x, "admin")
// Setup: alice and bob are Person
// Expect: only (alice, specialRole, admin) -- bob does not match equal
// ============================================================================
void test_builtin_equal() {
    TEST("Built-in function: equal");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));
    storage->addIndividual(makeInd("bob", "Bob", "Person"));

    SwrlRule rule;
    rule.id = "r1";
    rule.name = "alice_admin";
    rule.body = {
        classAtom("Person", "?x"),
        builtInAtom("swrlb:equal", {"?x", "alice"})
    };
    rule.head = { objPropAtom("specialRole", "?x", "admin") };

    SwrlEngine engine(storage);
    engine.addRule(rule);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 1u);
    ASSERT_EQ(inferred[0].subject, "alice");
    ASSERT_EQ(inferred[0].predicate, "specialRole");
    ASSERT_EQ(inferred[0].object, "admin");

    PASS();
}

// ============================================================================
// Test 5: Termination at fixpoint (tautological rule)
// manages(?x, ?y) -> manages(?x, ?y)
// Setup: (alice, manages, bob) already in storage
// Expect: no new facts (triple already exists, engine stops)
// ============================================================================
void test_termination_at_fixpoint() {
    TEST("Termination at fixpoint (tautological rule)");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Manager"));
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    SwrlRule rule;
    rule.id = "r1";
    rule.name = "tautology";
    rule.body = { objPropAtom("manages", "?x", "?y") };
    rule.head = { objPropAtom("manages", "?x", "?y") };

    SwrlEngine engine(storage);
    engine.addRule(rule);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 0u);

    PASS();
}

// ============================================================================
// Test 6: Multiple rules interact independently
// Rule1: Person(?x) -> hasType(?x, "Human")
// Rule2: Person(?x) -> hasStatus(?x, "Active")
// Setup: alice is Person
// Expect: (alice, hasType, Human) and (alice, hasStatus, Active)
// ============================================================================
void test_multiple_rules_interact() {
    TEST("Multiple rules interact independently");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    SwrlRule rule1;
    rule1.id = "r1";
    rule1.name = "person_human";
    rule1.body = { classAtom("Person", "?x") };
    rule1.head = { objPropAtom("hasType", "?x", "Human") };

    SwrlRule rule2;
    rule2.id = "r2";
    rule2.name = "person_active";
    rule2.body = { classAtom("Person", "?x") };
    rule2.head = { objPropAtom("hasStatus", "?x", "Active") };

    SwrlEngine engine(storage);
    engine.addRule(rule1);
    engine.addRule(rule2);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 2u);

    bool foundHuman = false, foundActive = false;
    for (const auto& t : inferred) {
        if (t.predicate == "hasType" && t.subject == "alice" && t.object == "Human")
            foundHuman = true;
        if (t.predicate == "hasStatus" && t.subject == "alice" && t.object == "Active")
            foundActive = true;
    }
    ASSERT_TRUE(foundHuman);
    ASSERT_TRUE(foundActive);

    PASS();
}

// ============================================================================
// Test 7: DataPropertyAtom in rule head
// Person(?x) -> age(?x, "unknown")
// Setup: alice is Person
// Expect: (alice, age, unknown) with isLiteral=true
// ============================================================================
void test_data_property_in_head() {
    TEST("DataProperty atom in rule head produces isLiteral triple");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    SwrlRule rule;
    rule.id = "r1";
    rule.name = "person_age";
    rule.body = { classAtom("Person", "?x") };
    rule.head = { dataPropAtom("age", "?x", "unknown") };

    SwrlEngine engine(storage);
    engine.addRule(rule);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 1u);
    ASSERT_EQ(inferred[0].subject, "alice");
    ASSERT_EQ(inferred[0].predicate, "age");
    ASSERT_EQ(inferred[0].object, "unknown");
    ASSERT_TRUE(inferred[0].isLiteral);

    PASS();
}

// ============================================================================
// Test 8: canApply returns true when body matches, false otherwise
// ============================================================================
void test_can_apply() {
    TEST("canApply returns correct status");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    SwrlRule rule1;
    rule1.id = "r1";
    rule1.body = { classAtom("Person", "?x") };
    rule1.head = { objPropAtom("worksAt", "?x", "Acme") };

    SwrlRule rule2;
    rule2.id = "r2";
    rule2.body = { classAtom("Manager", "?x") };
    rule2.head = { objPropAtom("manages", "?x", "team") };

    SwrlEngine engine(storage);
    engine.addRule(rule1);
    engine.addRule(rule2);

    ASSERT_TRUE(engine.canApply(rule1));
    ASSERT_TRUE(!engine.canApply(rule2));

    PASS();
}

// ============================================================================
// Test 9: Disabled rule is skipped during inference
// ============================================================================
void test_disabled_rule_skipped() {
    TEST("Disabled rule is skipped during inference");

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    storage->addIndividual(makeInd("alice", "Alice", "Person"));

    SwrlRule rule;
    rule.id = "r1";
    rule.name = "person_works";
    rule.body = { classAtom("Person", "?x") };
    rule.head = { objPropAtom("worksAt", "?x", "Acme") };
    rule.enabled = false;

    SwrlEngine engine(storage);
    engine.addRule(rule);

    auto inferred = engine.infer();

    ASSERT_EQ(inferred.size(), 0u);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SWRL Forward Chaining Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    test_simple_rule_triggers_new_fact();
    test_chain_reasoning();
    test_variable_binding_propagation();
    test_builtin_equal();
    test_termination_at_fixpoint();
    test_multiple_rules_interact();
    test_data_property_in_head();
    test_can_apply();
    test_disabled_rule_skipped();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
