#include "TestUtils.hpp"
#include <ontology/Config.hpp>
#include <fstream>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Test: Serialization roundtrip
// ============================================================================
void testSerializationRoundtrip() {
    TEST("OntologyConfig serialization roundtrip");

    OntologyConfig original;
    // Set non-default values
    original.server.port = 9090;
    original.server.host = "127.0.0.1";
    original.reasoner.hybrid.symbolicWeight = 0.7f;
    original.reasoner.hybrid.neuralWeight = 0.3f;
    original.reasoner.neural.embeddingDimension = 256;
    original.reasoner.neural.embeddingModel = "RotatE";
    original.storage.neo4j.enabled = true;
    original.storage.neo4j.uri = "bolt://prod:7687";
    original.rag.chunkSize = 1000;
    original.rag.bm25Weight = 0.5f;

    // toJson -> fromJson -> compare
    Json j = original.toJson();
    OntologyConfig restored = OntologyConfig::fromJson(j);

    ASSERT_EQ(restored.server.port, 9090);
    ASSERT_EQ(restored.server.host, "127.0.0.1");
    ASSERT_NEAR(restored.reasoner.hybrid.symbolicWeight, 0.7f, 0.001f);
    ASSERT_NEAR(restored.reasoner.hybrid.neuralWeight, 0.3f, 0.001f);
    ASSERT_EQ(restored.reasoner.neural.embeddingDimension, 256);
    ASSERT_EQ(restored.reasoner.neural.embeddingModel, "RotatE");
    ASSERT_TRUE(restored.storage.neo4j.enabled);
    ASSERT_EQ(restored.storage.neo4j.uri, "bolt://prod:7687");
    ASSERT_EQ(restored.rag.chunkSize, 1000);
    ASSERT_NEAR(restored.rag.bm25Weight, 0.5f, 0.001f);

    PASS();
}

// ============================================================================
// Test: Partial config - missing fields should default
// ============================================================================
void testPartialConfig() {
    TEST("OntologyConfig partial config defaults");

    Json j;
    j["server"]["port"] = 3000;
    // No other fields set

    OntologyConfig config = OntologyConfig::fromJson(j);

    // Server port should be what we set
    ASSERT_EQ(config.server.port, 3000);

    // Everything else should be defaults
    ASSERT_EQ(config.server.host, "0.0.0.0");
    ASSERT_EQ(config.reasoner.neural.embeddingModel, "TransE");
    ASSERT_EQ(config.reasoner.neural.embeddingDimension, 128);
    ASSERT_NEAR(config.reasoner.hybrid.symbolicWeight, 0.5f, 0.001f);
    ASSERT_TRUE(!config.storage.neo4j.enabled);
    ASSERT_EQ(config.rag.chunkSize, 500);

    PASS();
}

// ============================================================================
// Test: Validate catches bad port
// ============================================================================
void testValidateBadPort() {
    TEST("OntologyConfig validate bad port");

    OntologyConfig config;
    config.server.port = -1;

    auto errors = config.validate();
    ASSERT_TRUE(!errors.empty());

    bool foundPortError = false;
    for (const auto& e : errors) {
        if (e.find("port") != String::npos) {
            foundPortError = true;
            break;
        }
    }
    ASSERT_TRUE(foundPortError);

    PASS();
}

// ============================================================================
// Test: Validate catches bad weights
// ============================================================================
void testValidateBadWeights() {
    TEST("OntologyConfig validate bad weights");

    OntologyConfig config;
    config.reasoner.hybrid.symbolicWeight = 2.0f;

    auto errors = config.validate();
    ASSERT_TRUE(!errors.empty());

    bool foundWeightError = false;
    for (const auto& e : errors) {
        if (e.find("symbolicWeight") != String::npos) {
            foundWeightError = true;
            break;
        }
    }
    ASSERT_TRUE(foundWeightError);

    PASS();
}

// ============================================================================
// Test: Load from nonexistent file returns defaults
// ============================================================================
void testMissingFile() {
    TEST("OntologyConfig load missing file");

    auto config = OntologyConfig::load("/nonexistent/path/config.json");

    // Should return default config
    ASSERT_EQ(config.server.port, 8080);
    ASSERT_EQ(config.server.host, "0.0.0.0");

    PASS();
}

// ============================================================================
// Test: Load from file with corrupt JSON returns defaults
// ============================================================================
void testCorruptJson() {
    TEST("OntologyConfig load corrupt JSON");

    // Write invalid JSON to a temp file
    const char* tmpPath = "/tmp/test_ontology_corrupt.json";
    {
        std::ofstream f(tmpPath);
        f << "{ this is not valid json }}}";
    }

    auto config = OntologyConfig::load(tmpPath);

    // Should return default config (parse failure caught internally)
    ASSERT_EQ(config.server.port, 8080);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Config Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testSerializationRoundtrip();
    testPartialConfig();
    testValidateBadPort();
    testValidateBadWeights();
    testMissingFile();
    testCorruptJson();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
