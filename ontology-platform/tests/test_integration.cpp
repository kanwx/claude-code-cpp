/**
 * 端到端集成测试 - 简化版
 * 测试配置和基础存储功能
 */

#include <iostream>
#include <cassert>
#include <ontology/Config.hpp>
#include <ontology/Storage.hpp>
#include <thread>
#include <chrono>

using namespace ontology;

// 测试计数
int testsPassed = 0;
int testsFailed = 0;

#define TEST(name) \
    std::cout << "Testing: " << name << "... " << std::flush;

#define PASS() \
    std::cout << "✓" << std::endl; \
    testsPassed++;

#define FAIL(msg) \
    std::cout << "✗ " << msg << std::endl; \
    testsFailed++;

// 辅助函数: 创建三元组
Triple makeTriple(const String& s, const String& p, const String& o, float conf = 1.0f) {
    Triple t;
    t.subject = s;
    t.predicate = p;
    t.object = o;
    t.confidence = conf;
    return t;
}

// ============================================================================
// 配置测试
// ============================================================================

void testConfig() {
    TEST("StorageConfig default values");

    StorageConfig config;
    assert(!config.neo4j.enabled);
    assert(!config.milvus.enabled);
    assert(!config.qdrant.enabled);

    PASS();

    TEST("StorageConfig JSON serialization");

    config.neo4j.enabled = true;
    config.neo4j.uri = "bolt://localhost:7687";
    config.milvus.enabled = true;
    config.milvus.host = "localhost";
    config.milvus.port = 19530;

    Json j = config.toJson();
    assert(j["neo4j"]["enabled"] == true);
    assert(j["neo4j"]["uri"] == "bolt://localhost:7687");
    assert(j["milvus"]["enabled"] == true);

    StorageConfig config2 = StorageConfig::fromJson(j);
    assert(config2.neo4j.enabled == true);
    assert(config2.neo4j.uri == "bolt://localhost:7687");

    PASS();

    TEST("OntologyConfig file save/load");

    OntologyConfig fullConfig;
    fullConfig.storage.neo4j.enabled = true;
    fullConfig.storage.neo4j.uri = "bolt://test:7687";
    fullConfig.server.port = 9000;

    fullConfig.save("/tmp/test_ontology_config.json");

    OntologyConfig loaded = OntologyConfig::load("/tmp/test_ontology_config.json");
    assert(loaded.storage.neo4j.enabled == true);
    assert(loaded.storage.neo4j.uri == "bolt://test:7687");
    assert(loaded.server.port == 9000);

    PASS();

    TEST("ReasonerConfig JSON serialization");

    ReasonerConfig rconfig;
    rconfig.symbolic.enabled = true;
    rconfig.symbolic.maxInferenceDepth = 15;
    rconfig.neural.embeddingModel = "TransR";

    Json rj = rconfig.toJson();
    assert(rj["symbolic"]["enabled"] == true);
    assert(rj["symbolic"]["maxInferenceDepth"] == 15);
    assert(rj["neural"]["embeddingModel"] == "TransR");

    PASS();
}

// ============================================================================
// 存储测试
// ============================================================================

void testStorage() {
    TEST("TripleStore basic operations");

    TripleStore ts;

    // 添加三元组
    ts.add(makeTriple("alice", "manages", "bob"));
    ts.add(makeTriple("bob", "manages", "charlie"));

    // 验证
    assert(ts.count() == 2);

    PASS();

    TEST("TripleStore pattern query");

    ts.add(makeTriple("a", "rel1", "b"));
    ts.add(makeTriple("a", "rel2", "c"));
    ts.add(makeTriple("b", "rel1", "c"));

    // 按主语查询
    auto bySubject = ts.findBySubject("a");
    assert(bySubject.size() >= 2);

    // 按谓词查询
    auto byPred = ts.findByPredicate("rel1");
    assert(byPred.size() >= 2);

    // 模式查询
    TripleStore::TriplePattern pattern;
    pattern.subject = "a";
    pattern.subjectIsVar = false;
    pattern.predicateIsVar = true;
    pattern.objectIsVar = true;

    auto results = ts.query(pattern);
    assert(results.size() >= 2);

    PASS();

    TEST("TripleStore path finding");

    TripleStore pathStore;
    pathStore.add(makeTriple("A", "next", "B"));
    pathStore.add(makeTriple("B", "next", "C"));
    pathStore.add(makeTriple("C", "next", "D"));

    auto paths = pathStore.findPath("A", "D", "next", 5);
    assert(!paths.empty());
    assert(paths[0].size() == 7);  // A, next, B, next, C, next, D (interleaved predicates)

    PASS();

    TEST("TripleStore contains and remove");

    TripleStore ts2;
    ts2.add(makeTriple("x", "y", "z"));
    assert(ts2.contains(makeTriple("x", "y", "z")));
    ts2.remove(makeTriple("x", "y", "z"));
    assert(!ts2.contains(makeTriple("x", "y", "z")));
    assert(ts2.count() == 0);

    PASS();
}

// ============================================================================
// 类和个体测试
// ============================================================================

void testOntology() {
    TEST("Class operations");

    Class personClass;
    personClass.id = "Person";
    personClass.name = "Person";
    personClass.description = "A person entity";
    personClass.superClasses = {"Agent"};

    assert(personClass.id == "Person");
    assert(personClass.name == "Person");

    PASS();

    TEST("Individual operations");

    Individual alice;
    alice.id = "alice";
    alice.name = "Alice";
    alice.classId = "Person";
    alice.properties["age"] = 30;
    alice.properties["department"] = "Engineering";

    assert(alice.id == "alice");
    assert(alice.classId == "Person");

    PASS();

    TEST("Relation operations");

    Relation manages;
    manages.id = "manages";
    manages.name = "manages";
    manages.domain = "Person";
    manages.range = "Person";

    assert(manages.id == "manages");
    assert(manages.domain == "Person");

    PASS();
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Ontology Platform Integration Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // 配置测试
    std::cout << "--- Config Tests ---" << std::endl;
    testConfig();
    std::cout << std::endl;

    // 存储测试
    std::cout << "--- Storage Tests ---" << std::endl;
    testStorage();
    std::cout << std::endl;

    // 本体测试
    std::cout << "--- Ontology Tests ---" << std::endl;
    testOntology();
    std::cout << std::endl;

    // 总结
    std::cout << "========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
