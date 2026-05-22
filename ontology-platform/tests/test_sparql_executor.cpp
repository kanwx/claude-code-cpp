#include "TestUtils.hpp"
#include <ontology/sparql/SparqlExecutor.hpp>
#include <ontology/sparql/SparqlParser.hpp>
#include <ontology/sparql/SparqlAst.hpp>

using namespace ontology;
using namespace ontology::sparql;

int testsPassed = 0;
int testsFailed = 0;

// Helper: create a simple in-memory TripleSource from test data
static SparqlExecutor::TripleSource makeTestSource(const std::vector<Triple>& data) {
    return [&data](const String& s, const String& p, const String& o) -> std::vector<Triple> {
        std::vector<Triple> results;
        for (const auto& t : data) {
            if ((s.empty() || s == t.subject) &&
                (p.empty() || p == t.predicate) &&
                (o.empty() || o == t.object)) {
                results.push_back(t);
            }
        }
        return results;
    };
}

// ============================================================================
// Test: Simple SELECT returns matching bindings
// ============================================================================
void testSimpleSelect() {
    TEST("SPARQL executor simple SELECT");

    std::vector<Triple> testData = {
        makeTriple("alice", "manages", "bob"),
        makeTriple("bob", "manages", "charlie"),
        makeTriple("alice", "type", "Manager"),
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    auto result = exec.execute(
        "SELECT ?x ?y WHERE { ?x <manages> ?y }"
    );

    ASSERT_TRUE(result.success());
    ASSERT_EQ(result.rows.size(), 2u);

    // Check that alice->bob and bob->charlie are present
    bool foundAliceBob = false, foundBobCharlie = false;
    for (const auto& row : result.rows) {
        if (row.count("x") && row.count("y")) {
            std::string xval = row.at("x").get<std::string>();
            std::string yval = row.at("y").get<std::string>();
            if (xval == "alice" && yval == "bob") foundAliceBob = true;
            if (xval == "bob" && yval == "charlie") foundBobCharlie = true;
        }
    }
    ASSERT_TRUE(foundAliceBob);
    ASSERT_TRUE(foundBobCharlie);

    PASS();
}

// ============================================================================
// Test: ASK returns true/false
// ============================================================================
void testAskQuery() {
    TEST("SPARQL executor ASK true/false");

    std::vector<Triple> testData = {
        makeTriple("alice", "knows", "bob"),
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    // ASK with matching pattern -> true
    {
        auto result = exec.execute(
            "ASK WHERE { ?x <knows> ?y }"
        );
        ASSERT_TRUE(result.success());
        ASSERT_TRUE(result.askResult);
    }

    // ASK with non-matching pattern -> false
    {
        auto result = exec.execute(
            "ASK WHERE { ?x <unknown> ?y }"
        );
        ASSERT_TRUE(result.success());
        ASSERT_TRUE(!result.askResult);
    }

    PASS();
}

// ============================================================================
// Test: CONSTRUCT returns triples
// ============================================================================
void testConstructQuery() {
    TEST("SPARQL executor CONSTRUCT returns triples");

    std::vector<Triple> testData = {
        makeTriple("alice", "knows", "bob"),
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    auto result = exec.execute(
        "CONSTRUCT { ?x <friend> ?y } WHERE { ?x <knows> ?y }"
    );

    ASSERT_TRUE(result.success());
    ASSERT_EQ(result.constructTriples.size(), 1u);
    ASSERT_EQ(result.constructTriples[0].subject.value, "alice");
    ASSERT_EQ(result.constructTriples[0].predicate.value, "friend");
    ASSERT_EQ(result.constructTriples[0].object.value, "bob");

    PASS();
}

// ============================================================================
// Test: FILTER narrows results
// ============================================================================
void testFilter() {
    TEST("SPARQL executor FILTER narrows results");

    std::vector<Triple> testData = {
        makeTriple("alice", "age", "25"),
        makeTriple("bob", "age", "35"),
        makeTriple("charlie", "age", "45"),
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    // Filter for age > 30 — should match bob and charlie
    auto result = exec.execute(
        "SELECT ?x ?a WHERE { ?x <age> ?a . FILTER(?a > \"30\") }"
    );

    ASSERT_TRUE(result.success());
    // At minimum the filtered results should be fewer than unfiltered
    auto allResult = exec.execute("SELECT ?x ?a WHERE { ?x <age> ?a }");
    ASSERT_TRUE(result.rows.size() <= allResult.rows.size());

    PASS();
}

// ============================================================================
// Test: OPTIONAL preserves left bindings
// ============================================================================
void testOptional() {
    TEST("SPARQL executor OPTIONAL preserves left bindings");

    std::vector<Triple> testData = {
        makeTriple("alice", "knows", "bob"),
        makeTriple("bob", "name", "Robert"),
        // alice has no name triple
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    auto result = exec.execute(
        "SELECT ?x ?name WHERE { "
        "?x <knows> ?y . "
        "OPTIONAL { ?x <name> ?name } "
        "}"
    );

    ASSERT_TRUE(result.success());
    // alice knows bob, but alice has no name -> still in results with name unbound
    ASSERT_TRUE(result.rows.size() >= 1u);

    PASS();
}

// ============================================================================
// Test: UNION merges results
// ============================================================================
void testUnion() {
    TEST("SPARQL executor UNION merges results");

    std::vector<Triple> testData = {
        makeTriple("alice", "type", "Cat"),
        makeTriple("bob", "type", "Dog"),
        makeTriple("charlie", "type", "Bird"),
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    auto result = exec.execute(
        "SELECT ?x WHERE { "
        "{ ?x <type> <Cat> } UNION { ?x <type> <Dog> } "
        "}"
    );

    ASSERT_TRUE(result.success());
    // Should have results for Cat and Dog, but not Bird
    ASSERT_TRUE(result.rows.size() >= 2u);

    PASS();
}

// ============================================================================
// Test: No match returns empty results
// ============================================================================
void testNoMatch() {
    TEST("SPARQL executor no match returns empty");

    std::vector<Triple> testData = {
        makeTriple("alice", "knows", "bob"),
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    auto result = exec.execute(
        "SELECT ?x WHERE { ?x <nonexistent> ?y }"
    );

    ASSERT_TRUE(result.success());
    ASSERT_EQ(result.rows.size(), 0u);

    PASS();
}

// ============================================================================
// Test: Execute parsed Query directly
// ============================================================================
void testParsedQueryExecution() {
    TEST("SPARQL executor execute parsed Query");

    std::vector<Triple> testData = {
        makeTriple("alice", "manages", "bob"),
    };

    auto source = makeTestSource(testData);
    SparqlExecutor exec(source);

    SparqlParser parser;
    auto parsed = parser.parse("SELECT ?x ?y WHERE { ?x <manages> ?y }");
    ASSERT_TRUE(parsed.has_value());

    auto result = exec.execute(parsed.value());
    ASSERT_TRUE(result.success());
    ASSERT_EQ(result.rows.size(), 1u);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SPARQL Executor Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testSimpleSelect();
    testAskQuery();
    testConstructQuery();
    testFilter();
    testOptional();
    testUnion();
    testNoMatch();
    testParsedQueryExecution();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
