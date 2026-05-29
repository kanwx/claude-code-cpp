#include "TestUtils.hpp"
#include <set>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Test 1: Basic Class CRUD
// ============================================================================
void testBasicClassCRUD() {
    TEST("HybridStorage basic Class CRUD");

    HybridStorage storage(nullptr, nullptr);

    // addClass + getClass
    Class cls;
    cls.id = "Person";
    cls.name = "Person";
    cls.description = "A human being";
    ASSERT_TRUE(storage.addClass(cls));
    ASSERT_EQ(storage.classCount(), 1u);

    auto retrieved = storage.getClass("Person");
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(retrieved->id, "Person");
    ASSERT_EQ(retrieved->name, "Person");
    ASSERT_EQ(retrieved->description, "A human being");

    // updateClass
    Class updated;
    updated.id = "Person";
    updated.name = "PersonUpdated";
    updated.description = "Updated desc";
    ASSERT_TRUE(storage.updateClass(updated));
    auto afterUpdate = storage.getClass("Person");
    ASSERT_TRUE(afterUpdate.has_value());
    ASSERT_EQ(afterUpdate->name, "PersonUpdated");

    // removeClass
    ASSERT_TRUE(storage.removeClass("Person"));
    ASSERT_EQ(storage.classCount(), 0u);
    ASSERT_TRUE(!storage.getClass("Person").has_value());

    // removeClass non-existent returns false
    ASSERT_TRUE(!storage.removeClass("NoSuchClass"));

    PASS();
}

// ============================================================================
// Test 2: Basic Individual CRUD
// ============================================================================
void testBasicIndividualCRUD() {
    TEST("HybridStorage basic Individual CRUD");

    HybridStorage storage(nullptr, nullptr);

    // addIndividual + getIndividual
    Individual ind;
    ind.id = "alice";
    ind.name = "Alice";
    ind.classId = "Person";
    ASSERT_TRUE(storage.addIndividual(ind));
    ASSERT_EQ(storage.individualCount(), 1u);

    auto retrieved = storage.getIndividual("alice");
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(retrieved->id, "alice");
    ASSERT_EQ(retrieved->name, "Alice");
    ASSERT_EQ(retrieved->classId, "Person");

    // updateIndividual
    Individual updated;
    updated.id = "alice";
    updated.name = "AliceUpdated";
    updated.classId = "Employee";
    ASSERT_TRUE(storage.updateIndividual(updated));
    auto afterUpdate = storage.getIndividual("alice");
    ASSERT_TRUE(afterUpdate.has_value());
    ASSERT_EQ(afterUpdate->name, "AliceUpdated");
    ASSERT_EQ(afterUpdate->classId, "Employee");

    // removeIndividual
    ASSERT_TRUE(storage.removeIndividual("alice"));
    ASSERT_EQ(storage.individualCount(), 0u);
    ASSERT_TRUE(!storage.getIndividual("alice").has_value());

    // removeIndividual non-existent returns false
    ASSERT_TRUE(!storage.removeIndividual("noSuchIndividual"));

    PASS();
}

// ============================================================================
// Test 3: Basic Triple CRUD
// ============================================================================
void testBasicTripleCRUD() {
    TEST("HybridStorage basic Triple CRUD");

    HybridStorage storage(nullptr, nullptr);

    // addTriple + findTriple
    ASSERT_TRUE(storage.addTriple(makeTriple("alice", "knows", "bob")));
    ASSERT_EQ(storage.tripleCount(), 1u);

    auto found = storage.findTriple("alice", "knows", "bob");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->subject, "alice");
    ASSERT_EQ(found->predicate, "knows");
    ASSERT_EQ(found->object, "bob");

    // findBySubject
    storage.addTriple(makeTriple("alice", "worksAt", "Acme"));
    auto bySubject = storage.findBySubject("alice");
    ASSERT_EQ(bySubject.size(), 2u);

    // removeTriple
    ASSERT_TRUE(storage.removeTriple(makeTriple("alice", "worksAt", "Acme")));
    ASSERT_EQ(storage.tripleCount(), 1u);
    ASSERT_TRUE(!storage.findTriple("alice", "worksAt", "Acme").has_value());

    // removeTriple non-existent returns false
    ASSERT_TRUE(!storage.removeTriple(makeTriple("x", "y", "z")));

    // getAllTriples
    auto all = storage.getAllTriples();
    ASSERT_EQ(all.size(), 1u);

    PASS();
}

// ============================================================================
// Test 4: Batch operations
// ============================================================================
void testBatchOperations() {
    TEST("HybridStorage batch operations");

    HybridStorage storage(nullptr, nullptr);

    // batchAddTriples
    std::vector<Triple> triples;
    for (int i = 0; i < 10; ++i) {
        triples.push_back(makeTriple("s" + std::to_string(i), "p", "o" + std::to_string(i)));
    }
    auto tripleResult = storage.batchAddTriples(triples);
    ASSERT_EQ(tripleResult.succeeded, 10);
    ASSERT_EQ(tripleResult.failed, 0);
    ASSERT_EQ(storage.tripleCount(), 10u);

    // batch add duplicate triples should fail
    auto dupResult = storage.batchAddTriples(triples);
    ASSERT_EQ(dupResult.succeeded, 0);
    ASSERT_EQ(dupResult.failed, 10);

    // batchAddClasses
    std::vector<Class> classes;
    for (int i = 0; i < 5; ++i) {
        Class cls;
        cls.id = "cls_" + std::to_string(i);
        cls.name = "Class_" + std::to_string(i);
        classes.push_back(cls);
    }
    auto classResult = storage.batchAddClasses(classes);
    ASSERT_EQ(classResult.succeeded, 5);
    ASSERT_EQ(classResult.failed, 0);
    ASSERT_EQ(storage.classCount(), 5u);

    // batchAddIndividuals
    std::vector<Individual> individuals;
    for (int i = 0; i < 7; ++i) {
        Individual ind;
        ind.id = "ind_" + std::to_string(i);
        ind.name = "Ind_" + std::to_string(i);
        ind.classId = "cls_0";
        individuals.push_back(ind);
    }
    auto indResult = storage.batchAddIndividuals(individuals);
    ASSERT_EQ(indResult.succeeded, 7);
    ASSERT_EQ(indResult.failed, 0);
    ASSERT_EQ(storage.individualCount(), 7u);

    PASS();
}

// ============================================================================
// Test 5: Transitive closure
// ============================================================================
void testTransitiveClosure() {
    TEST("HybridStorage transitive closure");

    HybridStorage storage(nullptr, nullptr);

    // Build: A subClassOf B, B subClassOf C, C subClassOf D
    storage.addTriple(makeTriple("A", "subClassOf", "B"));
    storage.addTriple(makeTriple("B", "subClassOf", "C"));
    storage.addTriple(makeTriple("C", "subClassOf", "D"));

    // Compute transitive closure for "subClassOf"
    auto closure = storage.computeTransitiveClosure("subClassOf", 10);

    // Should contain: (A,B), (B,C), (C,D) plus (A,C), (A,D), (B,D)
    ASSERT_EQ(closure.size(), 6u);

    // Verify all expected pairs are present
    std::set<std::pair<String, String>> closureSet(closure.begin(), closure.end());
    ASSERT_TRUE(closureSet.count({"A", "B"}) > 0);
    ASSERT_TRUE(closureSet.count({"B", "C"}) > 0);
    ASSERT_TRUE(closureSet.count({"C", "D"}) > 0);
    ASSERT_TRUE(closureSet.count({"A", "C"}) > 0);
    ASSERT_TRUE(closureSet.count({"A", "D"}) > 0);
    ASSERT_TRUE(closureSet.count({"B", "D"}) > 0);

    PASS();
}

// ============================================================================
// Test 6: SubClassOf index
// ============================================================================
void testSubClassOfIndex() {
    TEST("HybridStorage subClassOf index");

    HybridStorage storage(nullptr, nullptr);

    // Build hierarchy: Animal <- Mammal <- Dog, Cat
    //                    Animal <- Bird
    storage.addTriple(makeTriple("Dog", "subClassOf", "Mammal"));
    storage.addTriple(makeTriple("Cat", "subClassOf", "Mammal"));
    storage.addTriple(makeTriple("Mammal", "subClassOf", "Animal"));
    storage.addTriple(makeTriple("Bird", "subClassOf", "Animal"));

    // getDirectSubClasses
    auto mammalDirect = storage.getDirectSubClasses("Mammal");
    ASSERT_EQ(mammalDirect.size(), 2u);

    auto animalDirect = storage.getDirectSubClasses("Animal");
    ASSERT_EQ(animalDirect.size(), 2u);

    // getDirectSubClasses for leaf class should return empty
    auto dogDirect = storage.getDirectSubClasses("Dog");
    ASSERT_EQ(dogDirect.size(), 0u);

    // getAllSubClasses for Animal should return Mammal, Bird, Dog, Cat
    auto animalAll = storage.getAllSubClasses("Animal");
    ASSERT_EQ(animalAll.size(), 4u);

    // getAllSubClasses for Mammal should return Dog, Cat
    auto mammalAll = storage.getAllSubClasses("Mammal");
    ASSERT_EQ(mammalAll.size(), 2u);

    // Verify the actual values in mammalAll
    std::set<String> mammalAllSet(mammalAll.begin(), mammalAll.end());
    ASSERT_TRUE(mammalAllSet.count("Dog") > 0);
    ASSERT_TRUE(mammalAllSet.count("Cat") > 0);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  HybridStorage Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testBasicClassCRUD();
    testBasicIndividualCRUD();
    testBasicTripleCRUD();
    testBatchOperations();
    testTransitiveClosure();
    testSubClassOfIndex();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
