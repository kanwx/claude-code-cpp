#include "TestUtils.hpp"
#include <ontology/storage/HybridStorage.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

void testSetReadOnly() {
    TEST("Set and check read-only mode");
    HybridStorage storage(nullptr, nullptr);
    ASSERT_TRUE(!storage.isReadOnly());
    storage.setReadOnly(true);
    ASSERT_TRUE(storage.isReadOnly());
    storage.setReadOnly(false);
    ASSERT_TRUE(!storage.isReadOnly());
    PASS();
}

void testWriteBlockedInReadOnly() {
    TEST("Writes blocked in read-only mode");
    HybridStorage storage(nullptr, nullptr);
    storage.setReadOnly(true);

    Class cls;
    cls.id = "Blocked";
    cls.name = "Blocked";
    ASSERT_TRUE(!storage.addClass(cls));

    Individual ind;
    ind.id = "Ind1";
    ind.name = "Ind1";
    ASSERT_TRUE(!storage.addIndividual(ind));

    Triple t = makeTriple("s", "p", "o");
    ASSERT_TRUE(!storage.addTriple(t));
    PASS();
}

void testReadAllowedInReadOnly() {
    TEST("Reads allowed in read-only mode");
    HybridStorage storage(nullptr, nullptr);

    // Add data before going read-only
    Class cls;
    cls.id = "Existing";
    cls.name = "Existing";
    storage.addClass(cls);
    storage.setReadOnly(true);

    // Reads should still work
    auto retrieved = storage.getClass("Existing");
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(retrieved->name, "Existing");
    PASS();
}

void testReadOnlyViaPublicMethods() {
    TEST("All public write methods respect read-only");
    HybridStorage storage(nullptr, nullptr);
    storage.setReadOnly(true);

    Class cls;
    cls.id = "C1";
    cls.name = "C1";
    ASSERT_TRUE(!storage.addClass(cls));
    ASSERT_TRUE(!storage.updateClass(cls));
    ASSERT_TRUE(!storage.removeClass("C1"));

    Relation rel;
    rel.id = "R1";
    rel.name = "R1";
    ASSERT_TRUE(!storage.addRelation(rel));

    Individual ind;
    ind.id = "I1";
    ind.name = "I1";
    ASSERT_TRUE(!storage.addIndividual(ind));
    ASSERT_TRUE(!storage.removeIndividual("I1"));

    Triple t = makeTriple("s", "p", "o");
    ASSERT_TRUE(!storage.addTriple(t));
    ASSERT_TRUE(!storage.removeTriple(t));
    PASS();
}

int main() {
    testSetReadOnly();
    testWriteBlockedInReadOnly();
    testReadAllowedInReadOnly();
    testReadOnlyViaPublicMethods();
    std::cout << "\n=== " << testsPassed << "/" << (testsPassed + testsFailed) << " tests passed ===\n";
    return testsFailed > 0 ? 1 : 0;
}
