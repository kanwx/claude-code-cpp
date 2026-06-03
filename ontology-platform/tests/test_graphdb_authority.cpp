#include "TestUtils.hpp"
#include <ontology/storage/HybridStorage.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

void testHybridStorageLoadFromGraphDB() {
    TEST("HybridStorage loadFromGraphDB with null graphDB");
    HybridStorage storage(nullptr, nullptr);
    ASSERT_TRUE(!storage.loadFromGraphDB());
    PASS();
}

void testHybridStorageWriteThroughNullGraphDB() {
    TEST("HybridStorage write-through with null graphDB");
    HybridStorage storage(nullptr, nullptr);
    Class cls;
    cls.id = "Animal";
    cls.name = "Animal";
    ASSERT_TRUE(storage.addClass(cls));
    auto retrieved = storage.getClass("Animal");
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(retrieved->name, "Animal");
    PASS();
}

void testHybridStorageReadOnlyMode() {
    TEST("HybridStorage read-only mode");
    HybridStorage storage(nullptr, nullptr);
    ASSERT_TRUE(!storage.isReadOnly());
    storage.setReadOnly(true);
    ASSERT_TRUE(storage.isReadOnly());
    Class cls;
    cls.id = "Plant";
    cls.name = "Plant";
    ASSERT_TRUE(!storage.addClass(cls));
    PASS();
}

void testHybridStorageGetSuperClasses() {
    TEST("HybridStorage getSuperClasses");
    HybridStorage storage(nullptr, nullptr);
    Class animal;
    animal.id = "Animal";
    animal.name = "Animal";
    Class mammal;
    mammal.id = "Mammal";
    mammal.name = "Mammal";
    mammal.superClasses = {"Animal"};
    storage.addClass(animal);
    storage.addClass(mammal);
    auto supers = storage.getSuperClasses("Mammal");
    ASSERT_EQ(supers.size(), 1u);
    ASSERT_EQ(supers[0], "Animal");
    PASS();
}

int main() {
    testHybridStorageLoadFromGraphDB();
    testHybridStorageWriteThroughNullGraphDB();
    testHybridStorageReadOnlyMode();
    testHybridStorageGetSuperClasses();
    std::cout << "\n=== " << testsPassed << "/" << (testsPassed + testsFailed) << " tests passed ===\n";
    return testsFailed > 0 ? 1 : 0;
}
