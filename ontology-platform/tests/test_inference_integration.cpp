#include "TestUtils.hpp"
#include <ontology/Storage.hpp>
#include <ontology/sparql/SparqlExecutor.hpp>
#include <ontology/sparql/SparqlParser.hpp>
#include <ontology/Swrl.hpp>
#include <ontology/ShaclValidation.hpp>
#include <ontology/Inference.hpp>

using namespace ontology;
using namespace ontology::sparql;

int testsPassed = 0;
int testsFailed = 0;

// Helper: resolve a variable through a binding chain.
// The backward chainer may bind goal vars to rule var names (e.g. ?b),
// which are then bound to concrete values under the rule var key (e.g. b -> "bob").
// This follows the chain: x -> "?b" -> b -> "bob".
static String resolveBinding(const String& var, const Binding& b, int maxDepth = 10) {
    String val = var;
    while (maxDepth-- > 0) {
        auto it = b.find(val);
        if (it == b.end()) {
            // Try stripping "?" prefix — backward chainer stores "?b" as key for var "b"
            if (!val.empty() && val[0] == '?') {
                String stripped = val.substr(1);
                it = b.find(stripped);
                if (it != b.end()) {
                    val = it->second;
                    continue;
                }
            }
            break;
        }
        val = it->second;
    }
    return val;
}

// Helper: create an in-memory HybridStorage
static std::shared_ptr<HybridStorage> makeStorage() {
    return std::make_shared<HybridStorage>(nullptr, nullptr);
}

// Helper: add a Class to storage
static void addClass(HybridStorage& storage, const String& id, const String& name,
                     const std::vector<String>& supers = {}) {
    Class cls;
    cls.id = id;
    cls.name = name;
    cls.superClasses = supers;
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
// Test 1: SPARQL query over SWRL-inferred facts
// ============================================================================
void test_sparql_over_swrl_inferred() {
    TEST("SPARQL query over SWRL-inferred facts");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    addIndividual(*storage, "bob", "Bob", "Person");
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    // SWRL rule: manages(?x, ?y) -> supervisedBy(?y, ?x)
    SwrlEngine engine(storage);
    SwrlRule rule;
    rule.id = "r1";
    rule.confidence = 1.0f;
    rule.enabled = true;

    SwrlAtom bodyAtom;
    bodyAtom.type = SwrlAtomType::ObjectPropertyAtom;
    bodyAtom.propertyId = "manages";
    bodyAtom.argument1 = "?x";
    bodyAtom.argument2 = "?y";
    rule.body.push_back(bodyAtom);

    SwrlAtom headAtom;
    headAtom.type = SwrlAtomType::ObjectPropertyAtom;
    headAtom.propertyId = "supervisedBy";
    headAtom.argument1 = "?y";
    headAtom.argument2 = "?x";
    rule.head.push_back(headAtom);

    engine.addRule(rule);
    auto inferred = engine.infer();

    // Should produce: bob supervisedBy alice
    ASSERT_TRUE(!inferred.empty());

    // Now query with SPARQL
    auto source = SparqlExecutor::fromHybridStorage(*storage);
    SparqlExecutor exec(source);
    auto result = exec.execute("SELECT ?x ?y WHERE { ?x <supervisedBy> ?y }");

    ASSERT_TRUE(result.success());
    ASSERT_TRUE(result.rows.size() >= 1u);

    bool found = false;
    for (const auto& row : result.rows) {
        if (row.count("x") && row.count("y")) {
            String xval = row.at("x").get<String>();
            String yval = row.at("y").get<String>();
            if (xval == "bob" && yval == "alice") found = true;
        }
    }
    ASSERT_TRUE(found);

    PASS();
}

// ============================================================================
// Test 2: SWRL forward chaining produces facts visible to SPARQL
// ============================================================================
void test_swrl_forward_chain_visible_to_sparql() {
    TEST("SWRL forward chaining produces facts visible to SPARQL");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "carol", "Carol", "Person");
    addIndividual(*storage, "dave", "Dave", "Person");

    // Rule: Person(?x) -> hasType(?x, "PersonType")
    SwrlEngine engine(storage);
    SwrlRule rule;
    rule.id = "r2";
    rule.confidence = 1.0f;
    rule.enabled = true;

    SwrlAtom bodyAtom;
    bodyAtom.type = SwrlAtomType::ClassAtom;
    bodyAtom.classId = "Person";
    bodyAtom.classArgument = "?x";
    rule.body.push_back(bodyAtom);

    SwrlAtom headAtom;
    headAtom.type = SwrlAtomType::DataPropertyAtom;
    headAtom.propertyId = "hasType";
    headAtom.argument1 = "?x";
    headAtom.argument2 = "\"PersonType\"";
    rule.head.push_back(headAtom);

    engine.addRule(rule);
    auto inferred = engine.infer();

    ASSERT_TRUE(!inferred.empty());

    // SPARQL should see the hasType triples
    auto source = SparqlExecutor::fromHybridStorage(*storage);
    SparqlExecutor exec(source);
    auto result = exec.execute("SELECT ?x WHERE { ?x <hasType> ?y }");

    ASSERT_TRUE(result.success());
    ASSERT_TRUE(result.rows.size() >= 1u);

    PASS();
}

// ============================================================================
// Test 3: SHACL validates after inference changes
// ============================================================================
void test_shacl_validates_after_inference() {
    TEST("SHACL validates after inference changes");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    // alice has no email -> violates shape

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "emailProp";
    prop.path = "email";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);

    // Initially should violate
    auto report1 = validator.validateNode("alice");
    ASSERT_TRUE(!report1.conforms);
    ASSERT_TRUE(report1.violationCount() >= 1);

    // Add email triple to storage
    storage->addTriple(makeTriple("alice", "email", "alice@example.com"));

    // Re-validate — should now conform
    auto report2 = validator.validateNode("alice");
    ASSERT_TRUE(report2.conforms);

    PASS();
}

// ============================================================================
// Test 4: Incremental reasoner fires on triple addition
// ============================================================================
void test_incremental_reasoner_fires_on_triple() {
    TEST("Incremental reasoner fires on triple addition");

    auto storage = makeStorage();
    addClass(*storage, "Employee", "Employee");
    addIndividual(*storage, "alice", "Alice", "Employee");
    addIndividual(*storage, "bob", "Bob", "Employee");

    // Set up a transitive relation "reportsTo"
    Relation rel;
    rel.id = "reportsTo";
    rel.name = "reportsTo";
    rel.isTransitive = true;
    storage->addRelation(rel);

    // Add base triples: alice reportsTo bob, bob reportsTo charlie
    storage->addTriple(makeTriple("alice", "reportsTo", "bob"));
    storage->addTriple(makeTriple("bob", "reportsTo", "charlie"));
    addIndividual(*storage, "charlie", "Charlie", "Employee");

    auto symbolic = std::make_shared<SymbolicReasoner>(storage);
    auto shacl = std::make_shared<ShaclValidator>(storage);

    IncrementalReasoner incr(storage, symbolic, shacl);
    // Forward chaining enabled by default, SHACL validation enabled by default

    // Add a new triple that doesn't trigger transitivity by itself
    auto delta = incr.addTriple(makeTriple("dave", "reportsTo", "alice"));
    addIndividual(*storage, "dave", "Dave", "Employee");

    // The delta should contain inferred transitive facts:
    // dave reportsTo bob (via alice), dave reportsTo charlie (via bob)
    // However, dave was not an individual known to storage before addTriple.
    // The IncrementalReasoner adds the triple and then forward-chains.
    // Since "reportsTo" is transitive, adding dave->alice should infer
    // dave->bob and dave->charlie (through the transitive chain).
    ASSERT_TRUE(delta.addedFacts.size() >= 1u);

    PASS();
}

// ============================================================================
// Test 5: Incremental reasoner detects SHACL violations
// ============================================================================
void test_incremental_reasoner_detects_shacl_violations() {
    TEST("Incremental reasoner detects SHACL violations");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");

    // SHACL shape: Person must have at least 1 "email"
    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "emailProp";
    prop.path = "email";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    auto shacl = std::make_shared<ShaclValidator>(storage);
    shacl->addShape(shape);

    auto symbolic = std::make_shared<SymbolicReasoner>(storage);
    IncrementalReasoner incr(storage, symbolic, shacl);

    // Add a triple about alice that does NOT provide an email.
    // This triggers validation and should find the violation.
    auto delta = incr.addTriple(makeTriple("alice", "worksAt", "AcmeCorp"));

    ASSERT_TRUE(!delta.violations.empty());

    PASS();
}

// ============================================================================
// Test 6: SWRL backward chaining + SPARQL integration
// ============================================================================
void test_swrl_backward_chain_sparql() {
    TEST("SWRL backward chaining + SPARQL integration");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    addIndividual(*storage, "bob", "Bob", "Person");
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    // Rule: manages(?a, ?b) -> supervisedBy(?b, ?a)
    // Use distinct variable names in rule to avoid collision with goal variables
    SwrlEngine engine(storage);
    SwrlRule rule;
    rule.id = "r6";
    rule.confidence = 1.0f;
    rule.enabled = true;

    SwrlAtom bodyAtom;
    bodyAtom.type = SwrlAtomType::ObjectPropertyAtom;
    bodyAtom.propertyId = "manages";
    bodyAtom.argument1 = "?a";
    bodyAtom.argument2 = "?b";
    rule.body.push_back(bodyAtom);

    SwrlAtom headAtom;
    headAtom.type = SwrlAtomType::ObjectPropertyAtom;
    headAtom.propertyId = "supervisedBy";
    headAtom.argument1 = "?b";
    headAtom.argument2 = "?a";
    rule.head.push_back(headAtom);

    engine.addRule(rule);

    // Backward chain: prove supervisedBy(?x, ?y)
    // Goal: find all ?x, ?y such that supervisedBy(?x, ?y) holds
    SwrlAtom goal;
    goal.type = SwrlAtomType::ObjectPropertyAtom;
    goal.propertyId = "supervisedBy";
    goal.argument1 = "?x";
    goal.argument2 = "?y";

    auto bindings = engine.backwardChain({goal}, 5);

    ASSERT_TRUE(!bindings.empty());

    // Verify that at least one binding resolves ?x to "bob" and ?y to "alice"
    // (since alice manages bob -> bob supervisedBy alice)
    // The backward chainer may bind goal vars to rule vars, so we need to
    // resolve through the binding chain.
    bool found = false;
    for (const auto& b : bindings) {
        String xval = resolveBinding("x", b);
        String yval = resolveBinding("y", b);
        if (xval == "bob" && yval == "alice") {
            found = true;
        }
    }
    ASSERT_TRUE(found);

    PASS();
}

// ============================================================================
// Test 7: Full pipeline: SPARQL -> SWRL -> SHACL
// ============================================================================
void test_full_pipeline_sparql_swrl_shacl() {
    TEST("Full pipeline: SPARQL -> SWRL -> SHACL");

    auto storage = makeStorage();
    addClass(*storage, "Employee", "Employee");
    addIndividual(*storage, "alice", "Alice", "Employee");
    addIndividual(*storage, "bob", "Bob", "Employee");
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    // Step 1: SPARQL query on initial state — should find manages relation
    {
        auto source = SparqlExecutor::fromHybridStorage(*storage);
        SparqlExecutor exec(source);
        auto result = exec.execute("SELECT ?x ?y WHERE { ?x <manages> ?y }");
        ASSERT_TRUE(result.success());
        ASSERT_EQ(result.rows.size(), 1u);
    }

    // Step 2: Run SWRL inference — manages(?a, ?b) -> supervisedBy(?b, ?a)
    {
        SwrlEngine engine(storage);
        SwrlRule rule;
        rule.id = "r7";
        rule.confidence = 1.0f;
        rule.enabled = true;

        SwrlAtom body;
        body.type = SwrlAtomType::ObjectPropertyAtom;
        body.propertyId = "manages";
        body.argument1 = "?a";
        body.argument2 = "?b";
        rule.body.push_back(body);

        SwrlAtom head;
        head.type = SwrlAtomType::ObjectPropertyAtom;
        head.propertyId = "supervisedBy";
        head.argument1 = "?b";
        head.argument2 = "?a";
        rule.head.push_back(head);

        engine.addRule(rule);
        auto inferred = engine.infer();
        ASSERT_TRUE(!inferred.empty());
    }

    // Step 3: SPARQL query should now see supervisedBy
    {
        auto source = SparqlExecutor::fromHybridStorage(*storage);
        SparqlExecutor exec(source);
        auto result = exec.execute("SELECT ?x ?y WHERE { ?x <supervisedBy> ?y }");
        ASSERT_TRUE(result.success());
        ASSERT_TRUE(result.rows.size() >= 1u);
    }

    // Step 4: SHACL validation on enriched data
    // Employee must have at least one "supervisedBy" link
    ShaclNodeShape shape;
    shape.id = "EmployeeShape";
    shape.targetClass = "Employee";
    ShaclPropertyShape prop;
    prop.id = "supervisedByProp";
    prop.path = "supervisedBy";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);

    // bob has supervisedBy (inferred), alice does not
    auto reportBob = validator.validateNode("bob");
    ASSERT_TRUE(reportBob.conforms);

    auto reportAlice = validator.validateNode("alice");
    ASSERT_TRUE(!reportAlice.conforms);

    PASS();
}

// ============================================================================
// Test 8: Storage query consistency after inference
// ============================================================================
void test_storage_query_consistency_after_inference() {
    TEST("Storage query consistency after inference");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    addIndividual(*storage, "bob", "Bob", "Person");
    storage->addTriple(makeTriple("alice", "knows", "bob"));

    // SWRL rule: knows(?a, ?b) -> knownBy(?b, ?a)
    SwrlEngine engine(storage);
    SwrlRule rule;
    rule.id = "r8";
    rule.confidence = 1.0f;
    rule.enabled = true;

    SwrlAtom body;
    body.type = SwrlAtomType::ObjectPropertyAtom;
    body.propertyId = "knows";
    body.argument1 = "?a";
    body.argument2 = "?b";
    rule.body.push_back(body);

    SwrlAtom head;
    head.type = SwrlAtomType::ObjectPropertyAtom;
    head.propertyId = "knownBy";
    head.argument1 = "?b";
    head.argument2 = "?a";
    rule.head.push_back(head);

    engine.addRule(rule);
    engine.infer();

    // Query storage directly
    auto directResults = storage->findBySP("bob", "knownBy");
    ASSERT_TRUE(!directResults.empty());

    bool directFound = false;
    for (const auto& t : directResults) {
        if (t.object == "alice") directFound = true;
    }
    ASSERT_TRUE(directFound);

    // Query same data via SPARQL
    auto source = SparqlExecutor::fromHybridStorage(*storage);
    SparqlExecutor exec(source);
    auto result = exec.execute("SELECT ?x WHERE { <bob> <knownBy> ?x }");

    ASSERT_TRUE(result.success());
    ASSERT_TRUE(result.rows.size() >= 1u);

    bool sparqlFound = false;
    for (const auto& row : result.rows) {
        if (row.count("x")) {
            if (row.at("x").get<String>() == "alice") sparqlFound = true;
        }
    }
    ASSERT_TRUE(sparqlFound);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    test_sparql_over_swrl_inferred();
    test_swrl_forward_chain_visible_to_sparql();
    test_shacl_validates_after_inference();
    test_incremental_reasoner_fires_on_triple();
    test_incremental_reasoner_detects_shacl_violations();
    test_swrl_backward_chain_sparql();
    test_full_pipeline_sparql_swrl_shacl();
    test_storage_query_consistency_after_inference();

    std::cout << "\nResults: " << testsPassed << " passed, " << testsFailed << " failed" << std::endl;
    return testsFailed > 0 ? 1 : 0;
}
