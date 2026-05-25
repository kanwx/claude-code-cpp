#include "TestUtils.hpp"
#include <ontology/sparql/SparqlEndpoint.hpp>
#include <ontology/Storage.hpp>

using namespace ontology;
using namespace ontology::sparql;

int testsPassed = 0;
int testsFailed = 0;

static std::shared_ptr<HybridStorage> makeStorage() {
    return std::make_shared<HybridStorage>(nullptr, nullptr);
}

void test_create_graph() {
    TEST("CREATE GRAPH");
    auto storage = makeStorage();
    SparqlEndpoint endpoint(storage);
    bool ok = endpoint.update("CREATE GRAPH <http://example.org/g1>");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(endpoint.hasNamedGraph("http://example.org/g1"));
    PASS();
}

void test_create_graph_idempotent() {
    TEST("CREATE GRAPH idempotent (no error on duplicate)");
    auto storage = makeStorage();
    SparqlEndpoint endpoint(storage);
    endpoint.update("CREATE GRAPH <http://example.org/g1>");
    bool ok = endpoint.update("CREATE GRAPH <http://example.org/g1>");
    ASSERT_TRUE(ok);
    PASS();
}

void test_drop_graph() {
    TEST("DROP GRAPH");
    auto storage = makeStorage();
    SparqlEndpoint endpoint(storage);
    endpoint.update("CREATE GRAPH <http://example.org/g1>");
    ASSERT_TRUE(endpoint.hasNamedGraph("http://example.org/g1"));
    bool ok = endpoint.update("DROP GRAPH <http://example.org/g1>");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(!endpoint.hasNamedGraph("http://example.org/g1"));
    PASS();
}

void test_drop_silent_missing() {
    TEST("DROP SILENT GRAPH (no error on missing)");
    auto storage = makeStorage();
    SparqlEndpoint endpoint(storage);
    bool ok = endpoint.update("DROP GRAPH <http://example.org/nonexistent>");
    ASSERT_TRUE(!ok);
    ok = endpoint.update("DROP SILENT GRAPH <http://example.org/nonexistent>");
    ASSERT_TRUE(ok);
    PASS();
}

void test_list_named_graphs() {
    TEST("List named graphs");
    auto storage = makeStorage();
    SparqlEndpoint endpoint(storage);
    endpoint.update("CREATE GRAPH <http://example.org/g1>");
    endpoint.update("CREATE GRAPH <http://example.org/g2>");
    auto graphs = endpoint.listNamedGraphs();
    ASSERT_EQ(graphs.size(), 2u);
    PASS();
}

void test_get_named_graph() {
    TEST("Get named graph TripleStore");
    auto storage = makeStorage();
    SparqlEndpoint endpoint(storage);
    endpoint.update("CREATE GRAPH <http://example.org/g1>");
    auto* ts = endpoint.getNamedGraph("http://example.org/g1");
    ASSERT_TRUE(ts != nullptr);
    ASSERT_EQ(ts->count(), 0u);
    PASS();
}

int main() {
    test_create_graph();
    test_create_graph_idempotent();
    test_drop_graph();
    test_drop_silent_missing();
    test_list_named_graphs();
    test_get_named_graph();
    std::cout << "\nSPARQL named graph tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
