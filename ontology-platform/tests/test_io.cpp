#include "TestUtils.hpp"
#include <ontology/OntologyIO.hpp>
#include <nlohmann/json.hpp>

using namespace ontology;
using Json = nlohmann::json;

int testsPassed = 0;
int testsFailed = 0;

void test_jsonld_basic_triples() {
    TEST("JSON-LD writer produces valid JSON-LD with @context and @graph");
    RdfGraph graph;
    graph.addPrefix("ex", "http://example.org/");
    graph.addPrefix("owl", "http://www.w3.org/2002/07/owl#");
    graph.addPrefix("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
    graph.addPrefix("rdfs", "http://www.w3.org/2000/01/rdf-schema#");

    graph.addTriple({"ex:Dog", "rdf:type", "owl:Class", false, "", ""});
    graph.addTriple({"ex:Animal", "rdf:type", "owl:Class", false, "", ""});
    graph.addTriple({"ex:Dog", "rdfs:subClassOf", "ex:Animal", false, "", ""});

    JsonLdWriter writer;
    String output = writer.writeRdf(graph);

    ASSERT_TRUE(output != "{}");
    ASSERT_TRUE(output.size() > 10);

    Json j;
    try {
        j = Json::parse(output);
    } catch (...) {
        FAIL("Output is not valid JSON");
        return;
    }

    ASSERT_TRUE(j.contains("@context"));
    ASSERT_TRUE(j.contains("@graph"));
    ASSERT_TRUE(j["@graph"].is_array());
    ASSERT_TRUE(j["@graph"].size() == 2);

    PASS();
}

void test_jsonld_literal_objects() {
    TEST("JSON-LD writer handles literal objects");
    RdfGraph graph;
    graph.addPrefix("ex", "http://example.org/");
    graph.addPrefix("rdfs", "http://www.w3.org/2000/01/rdf-schema#");

    graph.addTriple({"ex:Dog", "rdfs:label", "Dog", true, "xsd:string", ""});

    JsonLdWriter writer;
    String output = writer.writeRdf(graph);
    Json j = Json::parse(output);

    ASSERT_TRUE(j.contains("@graph"));
    bool foundLiteral = false;
    for (const auto& node : j["@graph"]) {
        if (node.contains("rdfs:label")) {
            foundLiteral = true;
            break;
        }
    }
    ASSERT_TRUE(foundLiteral);
    PASS();
}

void test_owlxml_basic_ontology() {
    TEST("OWL/XML writer produces native OWL/XML (not RDF/XML)");
    RdfGraph graph;
    graph.addPrefix("ex", "http://example.org/");
    graph.addPrefix("owl", "http://www.w3.org/2002/07/owl#");
    graph.addPrefix("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
    graph.addPrefix("rdfs", "http://www.w3.org/2000/01/rdf-schema#");

    graph.addTriple({"ex:Dog", "rdf:type", "owl:Class", false, "", ""});
    graph.addTriple({"ex:Animal", "rdf:type", "owl:Class", false, "", ""});
    graph.addTriple({"ex:Dog", "rdfs:subClassOf", "ex:Animal", false, "", ""});
    graph.addTriple({"ex:hasPart", "rdf:type", "owl:ObjectProperty", false, "", ""});
    graph.addTriple({"ex:hasPart", "rdfs:domain", "ex:Dog", false, "", ""});
    graph.addTriple({"ex:Dog", "owl:disjointWith", "ex:Cat", false, "", ""});

    OwlXmlWriter writer;
    String output = writer.writeRdf(graph);

    // Should NOT contain RDF/XML header
    ASSERT_TRUE(output.find("<rdf:RDF") == String::npos);
    // Should contain OWL/XML elements
    ASSERT_TRUE(output.find("<Ontology") != String::npos);
    ASSERT_TRUE(output.find("<Declaration>") != String::npos);
    ASSERT_TRUE(output.find("<SubClassOf>") != String::npos);
    ASSERT_TRUE(output.find("<Class") != String::npos);

    PASS();
}

void test_owlxml_individuals() {
    TEST("OWL/XML writer produces ClassAssertion and ObjectPropertyAssertion");
    RdfGraph graph;
    graph.addPrefix("ex", "http://example.org/");
    graph.addPrefix("owl", "http://www.w3.org/2002/07/owl#");
    graph.addPrefix("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");

    graph.addTriple({"ex:Dog", "rdf:type", "owl:Class", false, "", ""});
    graph.addTriple({"ex:rex", "rdf:type", "ex:Dog", false, "", ""});
    graph.addTriple({"ex:hasOwner", "rdf:type", "owl:ObjectProperty", false, "", ""});
    graph.addTriple({"ex:rex", "ex:hasOwner", "ex:alice", false, "", ""});

    OwlXmlWriter writer;
    String output = writer.writeRdf(graph);

    ASSERT_TRUE(output.find("<ClassAssertion") != String::npos);
    ASSERT_TRUE(output.find("<ObjectPropertyAssertion") != String::npos);
    PASS();
}

int main() {
    test_jsonld_basic_triples();
    test_jsonld_literal_objects();
    test_owlxml_basic_ontology();
    test_owlxml_individuals();

    std::cout << "\nIO tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
