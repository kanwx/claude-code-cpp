#include "TestUtils.hpp"
#include <ontology/DlReasoner.hpp>
#include <ontology/Storage.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

void test_satisfiable_atomic() {
    TEST("Atomic class is satisfiable");
    DlReasoner reasoner;
    ASSERT_TRUE(reasoner.isSatisfiable(ClassExpression::atomic("Person")));
    PASS();
}

void test_unsatisfiable_bottom() {
    TEST("Bottom is unsatisfiable");
    DlReasoner reasoner;
    ASSERT_TRUE(!reasoner.isSatisfiable(ClassExpression::bottom()));
    PASS();
}

void test_satisfiable_top() {
    TEST("Top is satisfiable");
    DlReasoner reasoner;
    ASSERT_TRUE(reasoner.isSatisfiable(ClassExpression::top()));
    PASS();
}

void test_unsatisfiable_contradiction() {
    TEST("A AND NOT-A is unsatisfiable");
    DlReasoner reasoner;
    auto aAndNotA = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::complement(ClassExpression::atomic("Cat"))
    });
    ASSERT_TRUE(!reasoner.isSatisfiable(aAndNotA));
    PASS();
}

void test_satisfiable_intersection() {
    TEST("A AND B is satisfiable when A and B are not disjoint");
    DlReasoner reasoner;
    auto aAndB = ClassExpression::intersection({
        ClassExpression::atomic("Dog"),
        ClassExpression::atomic("Pet")
    });
    ASSERT_TRUE(reasoner.isSatisfiable(aAndB));
    PASS();
}

void test_unsatisfiable_disjoint_intersection() {
    TEST("A AND B is unsatisfiable when A and B are disjoint");
    DlReasoner reasoner;
    reasoner.addDisjointClasses(ClassExpression::atomic("Cat"),
                                ClassExpression::atomic("Dog"));
    auto catAndDog = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::atomic("Dog")
    });
    ASSERT_TRUE(!reasoner.isSatisfiable(catAndDog));
    PASS();
}

void test_subsumption_with_tbox() {
    TEST("Subsumption: Dog subsumed-by Animal via TBox");
    DlReasoner reasoner;
    reasoner.addSubClassOf(ClassExpression::atomic("Dog"),
                           ClassExpression::atomic("Animal"));
    ASSERT_TRUE(reasoner.isSubsumedBy(ClassExpression::atomic("Dog"),
                                      ClassExpression::atomic("Animal")));
    ASSERT_TRUE(!reasoner.isSubsumedBy(ClassExpression::atomic("Animal"),
                                       ClassExpression::atomic("Dog")));
    PASS();
}

void test_equivalence() {
    TEST("Equivalence: Person equivalent-to Human");
    DlReasoner reasoner;
    reasoner.addEquivalentClasses(ClassExpression::atomic("Person"),
                                  ClassExpression::atomic("Human"));
    ASSERT_TRUE(reasoner.isEquivalent(ClassExpression::atomic("Person"),
                                      ClassExpression::atomic("Human")));
    PASS();
}

void test_consistency() {
    TEST("Consistent ABox");
    DlReasoner reasoner;
    reasoner.addConceptAssertion("alice", ClassExpression::atomic("Person"));
    reasoner.addConceptAssertion("bob", ClassExpression::atomic("Dog"));
    ASSERT_TRUE(reasoner.isConsistent());
    PASS();
}

void test_inconsistent_abox() {
    TEST("Inconsistent ABox (disjoint class assertions)");
    DlReasoner reasoner;
    reasoner.addDisjointClasses(ClassExpression::atomic("Cat"),
                                ClassExpression::atomic("Dog"));
    reasoner.addConceptAssertion("fluffy", ClassExpression::atomic("Cat"));
    reasoner.addConceptAssertion("fluffy", ClassExpression::atomic("Dog"));
    ASSERT_TRUE(!reasoner.isConsistent());
    PASS();
}

void test_classify() {
    TEST("Classification produces hierarchy");
    DlReasoner reasoner;
    reasoner.addSubClassOf(ClassExpression::atomic("Dog"),
                           ClassExpression::atomic("Animal"));
    reasoner.addSubClassOf(ClassExpression::atomic("Cat"),
                           ClassExpression::atomic("Animal"));
    auto hierarchy = reasoner.classify();
    ASSERT_TRUE(hierarchy.count("Dog") > 0);
    ASSERT_TRUE(hierarchy.count("Cat") > 0);
    bool foundDogAnimal = false;
    for (const auto& sup : hierarchy["Dog"]) {
        if (sup == "Animal") foundDogAnimal = true;
    }
    ASSERT_TRUE(foundDogAnimal);
    PASS();
}

void test_load_from_triplestore() {
    TEST("Load TBox from TripleStore");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"Cat", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"Cat", "http://www.w3.org/2002/07/owl#disjointWith", "Dog"});

    DlReasoner reasoner;
    reasoner.loadFromTripleStore(ts);

    ASSERT_TRUE(reasoner.isSubsumedBy(ClassExpression::atomic("Dog"),
                                      ClassExpression::atomic("Animal")));
    auto catAndDog = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::atomic("Dog")
    });
    ASSERT_TRUE(!reasoner.isSatisfiable(catAndDog));
    PASS();
}

void test_existential_rule() {
    TEST("EXISTS R.A is satisfiable");
    DlReasoner reasoner;
    auto existsRA = ClassExpression::someValuesFrom("hasPart",
        ClassExpression::atomic("Wheel"));
    ASSERT_TRUE(reasoner.isSatisfiable(existsRA));
    PASS();
}

void test_universal_rule() {
    TEST("FORALL R.Bottom WITH EXISTS R.Top is unsatisfiable");
    DlReasoner reasoner;
    auto allBottom = ClassExpression::allValuesFrom("hasPart", ClassExpression::bottom());
    auto existsTop = ClassExpression::someValuesFrom("hasPart", ClassExpression::top());
    auto both = ClassExpression::intersection({allBottom, existsTop});
    ASSERT_TRUE(!reasoner.isSatisfiable(both));
    PASS();
}

int main() {
    test_satisfiable_atomic();
    test_unsatisfiable_bottom();
    test_satisfiable_top();
    test_unsatisfiable_contradiction();
    test_satisfiable_intersection();
    test_unsatisfiable_disjoint_intersection();
    test_subsumption_with_tbox();
    test_equivalence();
    test_consistency();
    test_inconsistent_abox();
    test_classify();
    test_load_from_triplestore();
    test_existential_rule();
    test_universal_rule();

    std::cout << "\nALC Reasoner tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
