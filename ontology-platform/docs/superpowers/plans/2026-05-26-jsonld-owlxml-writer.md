# JSON-LD Writer + OWL/XML Writer Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the JSON-LD writer (currently returns literal `"{}"`) and the OWL/XML writer (currently delegates to RDF/XML instead of producing native OWL/XML).

**Architecture:** JSON-LD writer groups triples by subject, builds `@context` from prefixes, and produces `@graph` array. OWL/XML writer maps RDF triples to OWL 2 XML structural elements (Declaration, SubClassOf, DisjointClasses, etc.). Both operate on the `RdfGraph` data model.

**Tech Stack:** C++17, nlohmann/json, existing `OntologyIO.hpp` writer classes.

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `src/io/OntologyIO.cpp:1428-1430` | Modify | Replace `return "{}"` with real JSON-LD serialization |
| `src/io/OntologyIO.cpp:2038-2046` | Modify | Replace RDF/XML delegation with native OWL/XML output |
| `tests/test_io.cpp` | Create | Round-trip write+parse tests for both formats |
| `tests/CMakeLists.txt` | Modify | Add test target |

---

### Task 1: JSON-LD Writer Implementation

**Files:**
- Modify: `src/io/OntologyIO.cpp:1428-1430`

- [ ] **Step 1: Write the failing test**

Create `tests/test_io.cpp` with:

```cpp
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

    // Should not be "{}"
    ASSERT_TRUE(output != "{}");
    ASSERT_TRUE(output.size() > 10);

    // Should be valid JSON
    Json j;
    try {
        j = Json::parse(output);
    } catch (...) {
        FAIL("Output is not valid JSON");
        return;
    }

    // Should have @context
    ASSERT_TRUE(j.contains("@context"));

    // Should have @graph
    ASSERT_TRUE(j.contains("@graph"));
    ASSERT_TRUE(j["@graph"].is_array());
    ASSERT_TRUE(j["@graph"].size() == 2);  // Dog and Animal

    PASS();
}

void test_jsonld_literal_objects() {
    TEST("JSON-LD writer handles literal objects with @value");
    RdfGraph graph;
    graph.addPrefix("ex", "http://example.org/");
    graph.addPrefix("rdfs", "http://www.w3.org/2000/01/rdf-schema#");

    graph.addTriple({"ex:Dog", "rdfs:label", "Dog", true, "xsd:string", ""});

    JsonLdWriter writer;
    String output = writer.writeRdf(graph);
    Json j = Json::parse(output);

    ASSERT_TRUE(j.contains("@graph"));
    // Literal should be a string value, not an @id
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

int main() {
    test_jsonld_basic_triples();
    test_jsonld_literal_objects();

    std::cout << "\nIO tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make test_io && ./tests/test_io`

Expected: FAIL — JSON-LD writer returns `"{}"`.

- [ ] **Step 3: Implement JSON-LD writer**

Replace the existing `JsonLdWriter::writeRdf` at line 1428 in `src/io/OntologyIO.cpp`:

Find:
```cpp
String JsonLdWriter::writeRdf(const RdfGraph& graph) {
    return "{}";
}
```

Replace with:
```cpp
String JsonLdWriter::writeRdf(const RdfGraph& graph) {
    using Json = nlohmann::json;
    Json result;

    // Build @context from prefixes
    Json context = Json::object();
    for (const auto& [prefix, iri] : graph.prefixes) {
        context[prefix] = iri;
    }
    result["@context"] = context;

    // Group triples by subject
    std::unordered_map<String, std::vector<RdfTriple>> bySubject;
    for (const auto& t : graph.triples) {
        bySubject[t.subject].push_back(t);
    }

    // Build @graph array
    Json graphArr = Json::array();
    for (const auto& [subject, triples] : bySubject) {
        Json node;
        node["@id"] = subject;

        for (const auto& t : triples) {
            // rdf:type -> @type
            String rdfType = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
            String fullPred = resolvePrefixInIri(t.predicate, graph.prefixes);

            if (fullPred == rdfType || t.predicate == "rdf:type" || t.predicate == "a") {
                if (!node.contains("@type")) {
                    node["@type"] = t.object;
                } else {
                    // Multiple types: convert to array
                    if (node["@type"].is_string()) {
                        String first = node["@type"].get<String>();
                        node["@type"] = Json::array();
                        node["@type"].push_back(first);
                    }
                    node["@type"].push_back(t.object);
                }
                continue;
            }

            // Property value
            if (t.isLiteral) {
                // Literal object
                if (t.datatype == "xsd:string" || t.datatype == "http://www.w3.org/2001/XMLSchema#string" || t.datatype.empty()) {
                    node[t.predicate] = t.object;
                } else if (t.datatype == "xsd:integer" || t.datatype == "http://www.w3.org/2001/XMLSchema#integer") {
                    try { node[t.predicate] = std::stoi(t.object); }
                    catch (...) { node[t.predicate] = t.object; }
                } else if (t.datatype == "xsd:float" || t.datatype == "xsd:double" ||
                           t.datatype == "http://www.w3.org/2001/XMLSchema#float" ||
                           t.datatype == "http://www.w3.org/2001/XMLSchema#double") {
                    try { node[t.predicate] = std::stod(t.object); }
                    catch (...) { node[t.predicate] = t.object; }
                } else if (!t.language.empty()) {
                    Json litObj;
                    litObj["@value"] = t.object;
                    litObj["@language"] = t.language;
                    node[t.predicate] = litObj;
                } else {
                    Json litObj;
                    litObj["@value"] = t.object;
                    litObj["@type"] = t.datatype;
                    node[t.predicate] = litObj;
                }
            } else {
                // IRI object
                if (!node.contains(t.predicate)) {
                    node[t.predicate] = Json::object();
                    node[t.predicate]["@id"] = t.object;
                } else {
                    // Multiple values for same predicate: convert to array
                    if (node[t.predicate].is_object()) {
                        Json first = node[t.predicate];
                        node[t.predicate] = Json::array();
                        node[t.predicate].push_back(first);
                    }
                    Json valObj;
                    valObj["@id"] = t.object;
                    node[t.predicate].push_back(valObj);
                }
            }
        }

        graphArr.push_back(node);
    }

    result["@graph"] = graphArr;
    return result.dump(2);
}

String JsonLdWriter::resolvePrefixInIri(const String& value, const PrefixMap& prefixes) {
    auto colonPos = value.find(':');
    if (colonPos != String::npos && colonPos > 0) {
        String prefix = value.substr(0, colonPos);
        String local = value.substr(colonPos + 1);
        auto it = prefixes.find(prefix);
        if (it != prefixes.end()) {
            return it->second + local;
        }
    }
    return value;
}
```

Also add the private helper declaration to `JsonLdWriter` class in `include/ontology/OntologyIO.hpp` after `public:` section and before closing `};` of JsonLdWriter:

```cpp
private:
    static String resolvePrefixInIri(const String& value, const PrefixMap& prefixes);
```

- [ ] **Step 4: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_io && ./tests/test_io`

Expected: Both JSON-LD tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/io/OntologyIO.cpp include/ontology/OntologyIO.hpp tests/test_io.cpp
git commit -m "feat(io): implement JSON-LD writer with @context, @graph, literal and IRI object handling"
```

---

### Task 2: OWL/XML Writer Implementation

**Files:**
- Modify: `src/io/OntologyIO.cpp:2038-2046`
- Modify: `tests/test_io.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_io.cpp` before `main()`:

```cpp
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

    // Should NOT start with RDF/XML header
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
```

Add calls in `main()`:
```cpp
    test_owlxml_basic_ontology();
    test_owlxml_individuals();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_io && ./tests/test_io`

Expected: FAIL — OWL/XML writer delegates to RDF/XML, so no OWL-specific elements appear.

- [ ] **Step 3: Implement OWL/XML writer**

Replace the existing `OwlXmlWriter` implementations at lines 2038-2046 in `src/io/OntologyIO.cpp`:

Find:
```cpp
OwlXmlWriter::OwlXmlWriter() {}

String OwlXmlWriter::write(const Ontology& ontology) {
    return RdfXmlWriter().write(ontology);
}

String OwlXmlWriter::writeRdf(const RdfGraph& graph) {
    return RdfXmlWriter().writeRdf(graph);
}
```

Replace with:
```cpp
OwlXmlWriter::OwlXmlWriter() {}

String OwlXmlWriter::write(const Ontology& ontology) {
    RdfGraph graph;
    // Convert ontology to RdfGraph first
    for (const auto& cls : ontology.classes) {
        graph.addTriple({cls.id, "rdf:type", "owl:Class", false, "", ""});
        if (!cls.description.empty()) {
            graph.addTriple({cls.id, "rdfs:comment", cls.description, true, "xsd:string", ""});
        }
        for (const auto& sup : cls.superClasses) {
            graph.addTriple({cls.id, "rdfs:subClassOf", sup, false, "", ""});
        }
    }
    for (const auto& rel : ontology.relations) {
        graph.addTriple({rel.id, "rdf:type", "owl:ObjectProperty", false, "", ""});
        if (!rel.domain.empty()) {
            graph.addTriple({rel.id, "rdfs:domain", rel.domain, false, "", ""});
        }
        if (!rel.range.empty()) {
            graph.addTriple({rel.id, "rdfs:range", rel.range, false, "", ""});
        }
    }
    return writeRdf(graph);
}

String OwlXmlWriter::writeRdf(const RdfGraph& graph) {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    oss << "<Ontology xmlns=\"http://www.w3.org/2002/07/owl#\"\n";
    oss << "          xmlns:owl=\"http://www.w3.org/2002/07/owl#\"\n";
    oss << "          xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"\n";
    oss << "          xmlns:rdfs=\"http://www.w3.org/2000/01/rdf-schema#\"\n";
    oss << "          xmlns:xsd=\"http://www.w3.org/2001/XMLSchema#\">\n";

    String owlClass = "http://www.w3.org/2002/07/owl#Class";
    String owlObjProp = "http://www.w3.org/2002/07/owl#ObjectProperty";
    String owlDatatypeProp = "http://www.w3.org/2002/07/owl#DatatypeProperty";
    String rdfType = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
    String subClassOf = "http://www.w3.org/2000/01/rdf-schema#subClassOf";
    String disjointWith = "http://www.w3.org/2002/07/owl#disjointWith";
    String domain = "http://www.w3.org/2000/01/rdf-schema#domain";
    String range = "http://www.w3.org/2000/01/rdf-schema#range";

    // Resolve prefixed names to full IRIs
    auto resolve = [&](const String& val) -> String {
        auto colonPos = val.find(':');
        if (colonPos != String::npos && colonPos > 0) {
            String prefix = val.substr(0, colonPos);
            String local = val.substr(colonPos + 1);
            auto it = graph.prefixes.find(prefix);
            if (it != graph.prefixes.end()) {
                return it->second + local;
            }
        }
        // Already a full IRI in angle brackets?
        if (val.size() > 2 && val.front() == '<' && val.back() == '>') {
            return val.substr(1, val.size() - 2);
        }
        return val;
    };

    // Categorize triples
    std::unordered_set<String> declaredClasses;
    std::unordered_set<String> declaredProperties;
    std::vector<RdfTriple> subClassTriples;
    std::vector<RdfTriple> disjointTriples;
    std::vector<RdfTriple> domainTriples;
    std::vector<RdfTriple> rangeTriples;
    std::vector<RdfTriple> typeTriples;    // for individuals
    std::vector<RdfTriple> otherTriples;   // object property assertions

    for (const auto& t : graph.triples) {
        String fullPred = resolve(t.predicate);
        String fullObj = resolve(t.object);

        if (fullPred == rdfType) {
            if (fullObj == owlClass) {
                declaredClasses.insert(resolve(t.subject));
            } else if (fullObj == owlObjProp) {
                declaredProperties.insert(resolve(t.subject));
            } else if (fullObj == owlDatatypeProp) {
                declaredProperties.insert(resolve(t.subject));
            } else {
                typeTriples.push_back(t);
            }
        } else if (fullPred == subClassOf) {
            subClassTriples.push_back(t);
        } else if (fullPred == disjointWith) {
            disjointTriples.push_back(t);
        } else if (fullPred == domain) {
            domainTriples.push_back(t);
        } else if (fullPred == range) {
            rangeTriples.push_back(t);
        } else {
            otherTriples.push_back(t);
        }
    }

    // Declarations
    for (const auto& iri : declaredClasses) {
        oss << "  <Declaration><Class IRI=\"" << escapeXml(iri) << "\"/></Declaration>\n";
    }
    for (const auto& iri : declaredProperties) {
        // Check if object or datatype property
        bool isObject = true;
        for (const auto& t : graph.triples) {
            if (resolve(t.subject) == iri && resolve(t.predicate) == rdfType && resolve(t.object) == owlDatatypeProp) {
                isObject = false;
                break;
            }
        }
        if (isObject) {
            oss << "  <Declaration><ObjectProperty IRI=\"" << escapeXml(iri) << "\"/></Declaration>\n";
        } else {
            oss << "  <Declaration><DataProperty IRI=\"" << escapeXml(iri) << "\"/></Declaration>\n";
        }
    }

    // SubClassOf
    for (const auto& t : subClassTriples) {
        String sub = resolve(t.subject);
        String sup = resolve(t.object);
        // Only output if both are declared classes or look like class IRIs
        oss << "  <SubClassOf>";
        oss << "<Class IRI=\"" << escapeXml(sub) << "\"/>";
        oss << "<Class IRI=\"" << escapeXml(sup) << "\"/>";
        oss << "</SubClassOf>\n";
    }

    // DisjointClasses
    for (const auto& t : disjointTriples) {
        String c1 = resolve(t.subject);
        String c2 = resolve(t.object);
        oss << "  <DisjointClasses>";
        oss << "<Class IRI=\"" << escapeXml(c1) << "\"/>";
        oss << "<Class IRI=\"" << escapeXml(c2) << "\"/>";
        oss << "</DisjointClasses>\n";
    }

    // ObjectPropertyDomain / DataPropertyDomain
    for (const auto& t : domainTriples) {
        String prop = resolve(t.subject);
        String cls = resolve(t.object);
        bool isObjProp = true;
        for (const auto& tt : graph.triples) {
            if (resolve(tt.subject) == prop && resolve(tt.predicate) == rdfType && resolve(tt.object) == owlDatatypeProp) {
                isObjProp = false; break;
            }
        }
        if (isObjProp) {
            oss << "  <ObjectPropertyDomain>";
            oss << "<ObjectProperty IRI=\"" << escapeXml(prop) << "\"/>";
            oss << "<Class IRI=\"" << escapeXml(cls) << "\"/>";
            oss << "</ObjectPropertyDomain>\n";
        } else {
            oss << "  <DataPropertyDomain>";
            oss << "<DataProperty IRI=\"" << escapeXml(prop) << "\"/>";
            oss << "<Class IRI=\"" << escapeXml(cls) << "\"/>";
            oss << "</DataPropertyDomain>\n";
        }
    }

    // ObjectPropertyRange / DataPropertyRange
    for (const auto& t : rangeTriples) {
        String prop = resolve(t.subject);
        String cls = resolve(t.object);
        bool isObjProp = true;
        for (const auto& tt : graph.triples) {
            if (resolve(tt.subject) == prop && resolve(tt.predicate) == rdfType && resolve(tt.object) == owlDatatypeProp) {
                isObjProp = false; break;
            }
        }
        if (isObjProp) {
            oss << "  <ObjectPropertyRange>";
            oss << "<ObjectProperty IRI=\"" << escapeXml(prop) << "\"/>";
            oss << "<Class IRI=\"" << escapeXml(cls) << "\"/>";
            oss << "</ObjectPropertyRange>\n";
        } else {
            oss << "  <DataPropertyRange>";
            oss << "<DataProperty IRI=\"" << escapeXml(prop) << "\"/>";
            oss << "<Datatype IRI=\"" << escapeXml(cls) << "\"/>";
            oss << "</DataPropertyRange>\n";
        }
    }

    // ClassAssertion (individual typed as non-owl:Class)
    for (const auto& t : typeTriples) {
        String ind = resolve(t.subject);
        String cls = resolve(t.object);
        oss << "  <ClassAssertion>";
        oss << "<Class IRI=\"" << escapeXml(cls) << "\"/>";
        oss << "<NamedIndividual IRI=\"" << escapeXml(ind) << "\"/>";
        oss << "</ClassAssertion>\n";
        // Also declare the individual
        oss << "  <Declaration><NamedIndividual IRI=\"" << escapeXml(ind) << "\"/></Declaration>\n";
    }

    // ObjectPropertyAssertion
    for (const auto& t : otherTriples) {
        if (t.isLiteral) continue;  // skip data property assertions for simplicity
        String subj = resolve(t.subject);
        String prop = resolve(t.predicate);
        String obj = resolve(t.object);
        // Only output if property was declared as ObjectProperty
        bool isObjProp = false;
        for (const auto& tt : graph.triples) {
            if (resolve(tt.subject) == prop && resolve(tt.predicate) == rdfType && resolve(tt.object) == owlObjProp) {
                isObjProp = true; break;
            }
        }
        if (isObjProp) {
            oss << "  <ObjectPropertyAssertion>";
            oss << "<ObjectProperty IRI=\"" << escapeXml(prop) << "\"/>";
            oss << "<NamedIndividual IRI=\"" << escapeXml(subj) << "\"/>";
            oss << "<NamedIndividual IRI=\"" << escapeXml(obj) << "\"/>";
            oss << "</ObjectPropertyAssertion>\n";
        }
    }

    oss << "</Ontology>\n";
    return oss.str();
}
```

- [ ] **Step 4: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_io && ./tests/test_io`

Expected: All 4 tests pass (2 JSON-LD + 2 OWL/XML).

- [ ] **Step 5: Commit**

```bash
git add src/io/OntologyIO.cpp tests/test_io.cpp
git commit -m "feat(io): implement native OWL/XML writer with Declaration, SubClassOf, DisjointClasses, ClassAssertion, ObjectPropertyAssertion"
```

---

### Task 3: Add test target to CMakeLists

**Files:**
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Append test target**

Add to end of `tests/CMakeLists.txt`:

```cmake
# Unit test: IO (JSON-LD + OWL/XML writers)
add_executable(test_io test_io.cpp)
target_link_libraries(test_io PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME io COMMAND test_io)
```

- [ ] **Step 2: Build and run all tests**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make test_io && ./tests/test_io`

Expected: All 4 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/CMakeLists.txt
git commit -m "test(io): add IO test target to CMakeLists"
```
