#include "TestUtils.hpp"
#include <ontology/Storage.hpp>
#include <ontology/ServiceContext.hpp>
#include <ontology/Inference.hpp>
#include <ontology/Explainability.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Test 1: ServiceContext creation and teardown
// ============================================================================
void testServiceContextLifecycle() {
    TEST("ServiceContext creation and teardown");

    auto ctx = std::make_shared<ServiceContext>();
    ASSERT_TRUE(ctx != nullptr);

    ctx->storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    ASSERT_TRUE(ctx->storage != nullptr);

    // Verify storage is usable
    Class cls;
    cls.id = "Person";
    cls.name = "Person";
    ASSERT_TRUE(ctx->storage->addClass(cls));
    auto retrieved = ctx->storage->getClass("Person");
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(retrieved->name, "Person");

    PASS();
}

// ============================================================================
// Test 2: HybridStorage CRUD via ServiceContext
// ============================================================================
void testStorageViaContext() {
    TEST("HybridStorage CRUD via ServiceContext");

    auto ctx = std::make_shared<ServiceContext>();
    ctx->storage = std::make_shared<HybridStorage>(nullptr, nullptr);

    // Add triple
    Triple t{"alice", "knows", "bob"};
    ASSERT_TRUE(ctx->storage->addTriple(t));

    // Verify
    auto found = ctx->storage->findTriple("alice", "knows", "bob");
    ASSERT_TRUE(found.has_value());

    // Add individual
    Individual ind;
    ind.id = "alice";
    ind.name = "Alice";
    ind.classId = "Person";
    ASSERT_TRUE(ctx->storage->addIndividual(ind));

    auto retrieved = ctx->storage->getIndividual("alice");
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(retrieved->name, "Alice");

    PASS();
}

// ============================================================================
// Test 3: ServiceContext shared_ptr lifecycle (no leaks or dangling refs)
// ============================================================================
void testContextSharedPtrLifecycle() {
    TEST("ServiceContext shared_ptr lifecycle");

    ServiceContextPtr ctx1 = std::make_shared<ServiceContext>();
    ctx1->storage = std::make_shared<HybridStorage>(nullptr, nullptr);

    // Copy shared_ptr
    ServiceContextPtr ctx2 = ctx1;
    ASSERT_EQ(ctx1.use_count(), 2);
    ASSERT_EQ(ctx2.use_count(), 2);

    // Storage should still be valid after one reference drops
    ctx1.reset();
    ASSERT_TRUE(ctx2 != nullptr);
    ASSERT_TRUE(ctx2->storage != nullptr);

    // Add data through surviving reference
    Triple t{"x", "y", "z"};
    ASSERT_TRUE(ctx2->storage->addTriple(t));
    ASSERT_TRUE(ctx2->storage->findTriple("x", "y", "z").has_value());

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Service Context Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testServiceContextLifecycle();
    testStorageViaContext();
    testContextSharedPtrLifecycle();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
