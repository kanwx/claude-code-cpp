#include "TestUtils.hpp"
#include <ontology/Inference.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// Helper: create a storage with Person class and one individual
static std::shared_ptr<HybridStorage> makeHybridTestStorage() {
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);

    Class personCls;
    personCls.id = "Person";
    personCls.name = "Person";
    storage->addClass(personCls);

    Individual ind;
    ind.id = "alice"; ind.name = "Alice"; ind.classId = "Person";
    storage->addIndividual(ind);

    ind.id = "bob"; ind.name = "Bob"; ind.classId = "Person";
    storage->addIndividual(ind);

    storage->addTriple(makeTriple("alice", "manages", "bob"));

    return storage;
}

// ============================================================================
// Test: Symbolic-only mode (neuralWeight=0)
// ============================================================================
void testSymbolicOnly() {
    TEST("HybridReasoner symbolic-only mode");

    auto storage = makeHybridTestStorage();

    auto symbolic = std::make_shared<SymbolicReasoner>(storage);
    auto neural = std::make_shared<NeuralReasoner>(storage, 16);

    HybridReasoner::Config config;
    config.enableSymbolic = true;
    config.enableNeural = false;
    config.symbolWeight = 1.0f;
    config.neuralWeight = 0.0f;

    HybridReasoner reasoner(symbolic, neural, config);

    auto result = reasoner.infer("alice");

    // Should have symbolic facts but no neural predictions
    ASSERT_TRUE(!result.symbolicFacts.empty());
    ASSERT_TRUE(result.neuralPredictions.empty());

    PASS();
}

// ============================================================================
// Test: Neural-only mode (symbolicWeight=0, enableSymbolic=false)
// ============================================================================
void testNeuralOnly() {
    TEST("HybridReasoner neural-only mode");

    auto storage = makeHybridTestStorage();

    auto symbolic = std::make_shared<SymbolicReasoner>(storage);
    auto neural = std::make_shared<NeuralReasoner>(storage, 16);

    HybridReasoner::Config config;
    config.enableSymbolic = false;
    config.enableNeural = true;
    config.symbolWeight = 0.0f;
    config.neuralWeight = 1.0f;

    HybridReasoner reasoner(symbolic, neural, config);

    // Train neural first so it has embeddings
    neural->trainEmbeddings(5, 0.01f);

    auto result = reasoner.infer("alice");

    // Should have no symbolic facts
    ASSERT_TRUE(result.symbolicFacts.empty());

    // Neural predictions may or may not be present depending on whether
    // alice has an embedding that matches other individuals.
    // The key thing is no symbolic results were produced.
    PASS();
}

// ============================================================================
// Test: Equal weights (both 0.5)
// ============================================================================
void testEqualWeights() {
    TEST("HybridReasoner equal weights");

    auto storage = makeHybridTestStorage();

    auto symbolic = std::make_shared<SymbolicReasoner>(storage);
    auto neural = std::make_shared<NeuralReasoner>(storage, 16);

    HybridReasoner::Config config;
    config.enableSymbolic = true;
    config.enableNeural = true;
    config.symbolWeight = 0.5f;
    config.neuralWeight = 0.5f;

    HybridReasoner reasoner(symbolic, neural, config);

    // Train neural first
    neural->trainEmbeddings(5, 0.01f);

    auto result = reasoner.infer("alice");

    // Symbolic facts should be present (type inference for alice)
    ASSERT_TRUE(!result.symbolicFacts.empty());

    // The combined results should be non-empty (from D-S fusion)
    // At minimum, the symbolic facts get fused
    ASSERT_TRUE(!result.combined.empty() || !result.dsFusionDetails.empty());

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  HybridReasoner Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testSymbolicOnly();
    testNeuralOnly();
    testEqualWeights();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
