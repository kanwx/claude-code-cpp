#include "TestUtils.hpp"

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Test: Basic CRUD operations
// ============================================================================
void testBasicCRUD() {
    TEST("TripleStore basic CRUD");

    TripleStore ts;

    // Add
    ASSERT_TRUE(ts.add(makeTriple("alice", "knows", "bob")));
    ASSERT_TRUE(ts.add(makeTriple("alice", "worksAt", "Acme")));
    ASSERT_EQ(ts.count(), 2u);

    // Contains
    ASSERT_TRUE(ts.contains(makeTriple("alice", "knows", "bob")));
    ASSERT_TRUE(!ts.contains(makeTriple("bob", "knows", "alice")));

    // Remove
    ASSERT_TRUE(ts.remove(makeTriple("alice", "worksAt", "Acme")));
    ASSERT_EQ(ts.count(), 1u);
    ASSERT_TRUE(!ts.contains(makeTriple("alice", "worksAt", "Acme")));

    PASS();
}

// ============================================================================
// Test: Index-based queries (findBySubject, findByPredicate, findByObject,
//       findBySP, findByPO)
// ============================================================================
void testIndexes() {
    TEST("TripleStore index queries");

    TripleStore ts;
    ts.add(makeTriple("alice", "knows", "bob"));
    ts.add(makeTriple("alice", "worksAt", "Acme"));
    ts.add(makeTriple("bob", "knows", "charlie"));
    ts.add(makeTriple("dave", "worksAt", "Acme"));

    // findBySubject
    auto bySubj = ts.findBySubject("alice");
    ASSERT_EQ(bySubj.size(), 2u);

    // findByPredicate
    auto byPred = ts.findByPredicate("knows");
    ASSERT_EQ(byPred.size(), 2u);

    // findByObject
    auto byObj = ts.findByObject("Acme");
    ASSERT_EQ(byObj.size(), 2u);

    // findBySP
    auto bySP = ts.findBySP("alice", "knows");
    ASSERT_EQ(bySP.size(), 1u);
    ASSERT_EQ(bySP[0].object, "bob");

    // findByPO
    auto byPO = ts.findByPO("worksAt", "Acme");
    ASSERT_EQ(byPO.size(), 2u);

    PASS();
}

// ============================================================================
// Test: Pattern query with TriplePattern
// ============================================================================
void testPatternQuery() {
    TEST("TripleStore pattern query");

    TripleStore ts;
    ts.add(makeTriple("a", "p1", "b"));
    ts.add(makeTriple("a", "p2", "c"));
    ts.add(makeTriple("b", "p1", "c"));
    ts.add(makeTriple("d", "p3", "e"));

    // Subject bound, rest variable
    TripleStore::TriplePattern pat1;
    pat1.subject = "a";
    pat1.subjectIsVar = false;
    pat1.predicateIsVar = true;
    pat1.objectIsVar = true;
    auto r1 = ts.query(pat1);
    ASSERT_EQ(r1.size(), 2u);

    // Predicate bound, rest variable
    TripleStore::TriplePattern pat2;
    pat2.predicate = "p1";
    pat2.subjectIsVar = true;
    pat2.predicateIsVar = false;
    pat2.objectIsVar = true;
    auto r2 = ts.query(pat2);
    ASSERT_EQ(r2.size(), 2u);

    // Object bound
    TripleStore::TriplePattern pat3;
    pat3.object = "c";
    pat3.subjectIsVar = true;
    pat3.predicateIsVar = true;
    pat3.objectIsVar = false;
    auto r3 = ts.query(pat3);
    ASSERT_EQ(r3.size(), 2u);

    // All bound (exact match)
    TripleStore::TriplePattern pat4;
    pat4.subject = "a";
    pat4.predicate = "p1";
    pat4.object = "b";
    pat4.subjectIsVar = false;
    pat4.predicateIsVar = false;
    pat4.objectIsVar = false;
    auto r4 = ts.query(pat4);
    ASSERT_EQ(r4.size(), 1u);

    PASS();
}

// ============================================================================
// Test: Path finding
// ============================================================================
void testPathFinding() {
    TEST("TripleStore path finding");

    TripleStore ts;
    ts.add(makeTriple("A", "next", "B"));
    ts.add(makeTriple("B", "next", "C"));
    ts.add(makeTriple("C", "next", "D"));

    auto paths = ts.findPath("A", "D", "next", 5);
    ASSERT_TRUE(!paths.empty());
    // Path should be: A, next, B, next, C, next, D  => size 7
    ASSERT_EQ(paths[0].size(), 7u);
    ASSERT_EQ(paths[0][0], "A");
    ASSERT_EQ(paths[0].back(), "D");

    PASS();
}

// ============================================================================
// Test: Path finding with disconnected nodes
// ============================================================================
void testPathDisconnected() {
    TEST("TripleStore path finding disconnected");

    TripleStore ts;
    ts.add(makeTriple("A", "next", "B"));
    ts.add(makeTriple("C", "next", "D"));  // disconnected component

    auto paths = ts.findPath("A", "D", "next", 5);
    ASSERT_TRUE(paths.empty());

    PASS();
}

// ============================================================================
// Test: Adding duplicate triple returns false
// ============================================================================
void testDuplicateAdd() {
    TEST("TripleStore duplicate add");

    TripleStore ts;
    ASSERT_TRUE(ts.add(makeTriple("x", "y", "z")));
    ASSERT_TRUE(!ts.add(makeTriple("x", "y", "z")));  // duplicate
    ASSERT_EQ(ts.count(), 1u);

    PASS();
}

// ============================================================================
// Test: Removing non-existent triple returns false
// ============================================================================
void testRemoveNonexistent() {
    TEST("TripleStore remove nonexistent");

    TripleStore ts;
    ts.add(makeTriple("a", "b", "c"));
    ASSERT_TRUE(!ts.remove(makeTriple("x", "y", "z")));
    ASSERT_EQ(ts.count(), 1u);

    PASS();
}

// ============================================================================
// Test: Empty store queries
// ============================================================================
void testEmptyStore() {
    TEST("TripleStore empty store");

    TripleStore ts;
    ASSERT_EQ(ts.count(), 0u);
    ASSERT_TRUE(ts.findBySubject("anything").empty());
    ASSERT_TRUE(ts.findByPredicate("anything").empty());
    ASSERT_TRUE(ts.findByObject("anything").empty());
    ASSERT_TRUE(!ts.contains(makeTriple("a", "b", "c")));
    ASSERT_TRUE(ts.find("a", "b", "c").has_value() == false);

    TripleStore::TriplePattern pat;
    pat.subjectIsVar = true;
    pat.predicateIsVar = true;
    pat.objectIsVar = true;
    ASSERT_TRUE(ts.query(pat).empty());

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  TripleStore Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testBasicCRUD();
    testIndexes();
    testPatternQuery();
    testPathFinding();
    testPathDisconnected();
    testDuplicateAdd();
    testRemoveNonexistent();
    testEmptyStore();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
