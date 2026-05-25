#include "TestUtils.hpp"
#include <ontology/ClassExpression.hpp>
#include <ontology/Storage.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

static std::shared_ptr<HybridStorage> makeStorage() {
    return std::make_shared<HybridStorage>(nullptr, nullptr);
}

void test_atomic_subsumption_with_tbox() {
    TEST("Atomic subsumption with TBox (rdfs:subClassOf)");
    auto storage = makeStorage();
    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"Cat", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    auto dog = ClassExpression::atomic("Dog");
    auto animal = ClassExpression::atomic("Animal");
    auto cat = ClassExpression::atomic("Cat");
    ASSERT_TRUE(ClassExpressionEvaluator::isSubsumedBy(*dog, *animal, ts));
    ASSERT_TRUE(ClassExpressionEvaluator::isSubsumedBy(*cat, *animal, ts));
    ASSERT_TRUE(!ClassExpressionEvaluator::isSubsumedBy(*dog, *cat, ts));
    PASS();
}

void test_intersection_subsumption_with_tbox() {
    TEST("Intersection subsumption with TBox");
    auto storage = makeStorage();
    auto* ts = storage->getTripleStore();
    ts->add({"Puppy", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Dog"});
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"Pet", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    auto dogAndPet = ClassExpression::intersection({
        ClassExpression::atomic("Dog"),
        ClassExpression::atomic("Pet")
    });
    auto animal = ClassExpression::atomic("Animal");
    ASSERT_TRUE(ClassExpressionEvaluator::isSubsumedBy(*dogAndPet, *animal, ts));
    PASS();
}

void test_union_subsumption_with_tbox() {
    TEST("Union subsumption with TBox");
    auto storage = makeStorage();
    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"Cat", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    auto dogOrCat = ClassExpression::union_({
        ClassExpression::atomic("Dog"),
        ClassExpression::atomic("Cat")
    });
    auto animal = ClassExpression::atomic("Animal");
    ASSERT_TRUE(ClassExpressionEvaluator::isSubsumedBy(*dogOrCat, *animal, ts));
    PASS();
}

void test_complement_subsumption_with_disjoint() {
    TEST("Complement subsumption with owl:disjointWith");
    auto storage = makeStorage();
    auto* ts = storage->getTripleStore();
    ts->add({"Male", "http://www.w3.org/2002/07/owl#disjointWith", "Female"});
    auto male = ClassExpression::atomic("Male");
    auto notFemale = ClassExpression::complement(ClassExpression::atomic("Female"));
    ASSERT_TRUE(ClassExpressionEvaluator::isSubsumedBy(*male, *notFemale, ts));
    PASS();
}

void test_equivalence_with_tbox() {
    TEST("Equivalence with TBox (owl:equivalentClass + subsumption)");
    auto storage = makeStorage();
    auto* ts = storage->getTripleStore();
    ts->add({"Person", "http://www.w3.org/2002/07/owl#equivalentClass", "Human"});
    auto person = ClassExpression::atomic("Person");
    auto human = ClassExpression::atomic("Human");
    ASSERT_TRUE(person->isEquivalent(*human, ts));
    PASS();
}

void test_isEmpty_with_disjoint() {
    TEST("isEmpty with owl:disjointWith");
    auto storage = makeStorage();
    auto* ts = storage->getTripleStore();
    ts->add({"Cat", "http://www.w3.org/2002/07/owl#disjointWith", "Dog"});
    auto catAndDog = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::atomic("Dog")
    });
    ASSERT_TRUE(ClassExpressionEvaluator::isEmpty(*catAndDog, ts));
    ASSERT_TRUE(!ClassExpressionEvaluator::isEmpty(*ClassExpression::atomic("Cat"), ts));
    PASS();
}

void test_isUniversal() {
    TEST("isUniversal with complement of empty");
    auto storage = makeStorage();
    auto* ts = storage->getTripleStore();
    ts->add({"Cat", "http://www.w3.org/2002/07/owl#disjointWith", "Dog"});
    auto catAndDog = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::atomic("Dog")
    });
    auto notCatAndDog = ClassExpression::complement(catAndDog);
    ASSERT_TRUE(ClassExpressionEvaluator::isUniversal(*notCatAndDog, ts));
    ASSERT_TRUE(ClassExpressionEvaluator::isUniversal(*ClassExpression::top()));
    PASS();
}

void test_backward_compat_json_overload() {
    TEST("Old Json-based isSubsumedBy still works");
    Json hierarchy = Json::object();
    hierarchy["Dog"] = Json::array({"Animal"});
    hierarchy["Cat"] = Json::array({"Animal"});
    auto dog = ClassExpression::atomic("Dog");
    auto animal = ClassExpression::atomic("Animal");
    ASSERT_TRUE(ClassExpressionEvaluator::isSubsumedBy(*dog, *animal, hierarchy));
    PASS();
}

void test_functional_syntax_nested() {
    TEST("FunctionalSyntax nested expressions parse correctly");

    // ObjectIntersectionOf( ObjectUnionOf( A B ) C )
    String input = "ObjectIntersectionOf( ObjectUnionOf( <A> <B> ) <C> )";
    auto result = FunctionalSyntaxParser::parse(input);

    ASSERT_TRUE(result != nullptr);
    ASSERT_TRUE(result->type == ExpressionType::Intersection);
    ASSERT_EQ(result->operands.size(), 2u);
    ASSERT_TRUE(result->operands[0]->type == ExpressionType::Union);
    ASSERT_EQ(result->operands[0]->operands.size(), 2u);
    ASSERT_TRUE(result->operands[0]->operands[0]->className == "A");
    ASSERT_TRUE(result->operands[0]->operands[1]->className == "B");
    ASSERT_TRUE(result->operands[1]->className == "C");

    PASS();
}

void test_functional_syntax_complement() {
    TEST("FunctionalSyntax complement expression");

    String input = "ObjectComplementOf( <Person> )";
    auto result = FunctionalSyntaxParser::parse(input);

    ASSERT_TRUE(result != nullptr);
    ASSERT_TRUE(result->type == ExpressionType::Complement);
    ASSERT_TRUE(result->complementOf != nullptr);
    ASSERT_TRUE(result->complementOf->className == "Person");

    PASS();
}

void test_functional_syntax_some_values() {
    TEST("FunctionalSyntax someValuesFrom expression");

    String input = "ObjectSomeValuesFrom( <hasPart> <Wheel> )";
    auto result = FunctionalSyntaxParser::parse(input);

    ASSERT_TRUE(result != nullptr);
    ASSERT_TRUE(result->type == ExpressionType::ObjectSomeValuesFrom);
    ASSERT_TRUE(result->property == "hasPart");
    ASSERT_TRUE(result->filler != nullptr);
    ASSERT_TRUE(result->filler->className == "Wheel");

    PASS();
}

int main() {
    test_atomic_subsumption_with_tbox();
    test_intersection_subsumption_with_tbox();
    test_union_subsumption_with_tbox();
    test_complement_subsumption_with_disjoint();
    test_equivalence_with_tbox();
    test_isEmpty_with_disjoint();
    test_isUniversal();
    test_backward_compat_json_overload();
    test_functional_syntax_nested();
    test_functional_syntax_complement();
    test_functional_syntax_some_values();
    std::cout << "\nClassExpression tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
