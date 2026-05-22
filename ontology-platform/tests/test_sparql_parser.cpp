#include "TestUtils.hpp"
#include <ontology/sparql/SparqlParser.hpp>
#include <ontology/sparql/SparqlAst.hpp>

using namespace ontology;
using namespace ontology::sparql;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Test: Simple SELECT with one triple pattern
// ============================================================================
void testSimpleSelect() {
    TEST("SPARQL parser simple SELECT");

    SparqlParser parser;
    auto result = parser.parse("SELECT ?x WHERE { ?x <http://example.org/p> ?y }");

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_EQ(q.variables.size(), 1u);
    ASSERT_EQ(q.variables[0].name, "x");
    ASSERT_EQ(q.where.triples.size(), 1u);
    ASSERT_TRUE(q.where.triples[0].subject.isVariable());
    ASSERT_EQ(q.where.triples[0].subject.value, "x");
    ASSERT_TRUE(q.where.triples[0].predicate.isIRI());
    ASSERT_TRUE(q.where.triples[0].object.isVariable());
    ASSERT_EQ(q.where.triples[0].object.value, "y");

    PASS();
}

// ============================================================================
// Test: Multiple patterns in WHERE
// ============================================================================
void testMultiplePatterns() {
    TEST("SPARQL parser multiple triple patterns");

    SparqlParser parser;
    auto result = parser.parse(
        "SELECT ?x ?y WHERE { "
        "?x <http://example.org/p1> ?y . "
        "?y <http://example.org/p2> ?z "
        "}"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_EQ(q.where.triples.size(), 2u);
    ASSERT_EQ(q.variables.size(), 2u);

    PASS();
}

// ============================================================================
// Test: FILTER expressions (=, !=, <, >)
// ============================================================================
void testFilterExpressions() {
    TEST("SPARQL parser FILTER expressions");

    SparqlParser parser;

    // FILTER with equality
    {
        auto result = parser.parse(
            "SELECT ?x WHERE { ?x <http://example.org/age> ?age . FILTER(?age = 30) }"
        );
        ASSERT_TRUE(result.has_value());
        auto& q = std::get<SelectQuery>(*result);
        ASSERT_TRUE(!q.where.filters.empty());
    }

    // FILTER with not-equal
    {
        auto result = parser.parse(
            "SELECT ?x WHERE { ?x <http://example.org/age> ?age . FILTER(?age != 30) }"
        );
        ASSERT_TRUE(result.has_value());
        auto& q = std::get<SelectQuery>(*result);
        ASSERT_TRUE(!q.where.filters.empty());
    }

    // FILTER with less-than
    {
        auto result = parser.parse(
            "SELECT ?x WHERE { ?x <http://example.org/age> ?age . FILTER(?age < 30) }"
        );
        ASSERT_TRUE(result.has_value());
        auto& q = std::get<SelectQuery>(*result);
        ASSERT_TRUE(!q.where.filters.empty());
    }

    // FILTER with greater-than
    {
        auto result = parser.parse(
            "SELECT ?x WHERE { ?x <http://example.org/age> ?age . FILTER(?age > 30) }"
        );
        ASSERT_TRUE(result.has_value());
        auto& q = std::get<SelectQuery>(*result);
        ASSERT_TRUE(!q.where.filters.empty());
    }

    PASS();
}

// ============================================================================
// Test: OPTIONAL
// ============================================================================
void testOptional() {
    TEST("SPARQL parser OPTIONAL");

    SparqlParser parser;
    auto result = parser.parse(
        "SELECT ?x ?name WHERE { "
        "?x <http://example.org/knows> ?y . "
        "OPTIONAL { ?y <http://example.org/name> ?name } "
        "}"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_TRUE(!q.where.optionalGroups.empty());
    ASSERT_EQ(q.where.optionalGroups.size(), 1u);
    ASSERT_EQ(q.where.optionalGroups[0].triples.size(), 1u);

    PASS();
}

// ============================================================================
// Test: UNION
// ============================================================================
void testUnion() {
    TEST("SPARQL parser UNION");

    SparqlParser parser;
    auto result = parser.parse(
        "SELECT ?x WHERE { "
        "{ ?x <http://example.org/type> <http://example.org/Cat> } "
        "UNION "
        "{ ?x <http://example.org/type> <http://example.org/Dog> } "
        "}"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_TRUE(!q.where.unionGroups.empty());
    // unionGroups is a vector of GroupPatterns, each element is one side
    ASSERT_EQ(q.where.unionGroups.size(), 2u);
    ASSERT_EQ(q.where.unionGroups[0].triples.size(), 1u);
    ASSERT_EQ(q.where.unionGroups[1].triples.size(), 1u);

    PASS();
}

// ============================================================================
// Test: ASK query
// ============================================================================
void testAskQuery() {
    TEST("SPARQL parser ASK query");

    SparqlParser parser;
    auto result = parser.parse(
        "ASK WHERE { ?x <http://example.org/knows> ?y }"
    );

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(queryType(*result), SparqlQueryType::ASK);
    auto& q = std::get<AskQuery>(*result);
    ASSERT_EQ(q.where.triples.size(), 1u);

    PASS();
}

// ============================================================================
// Test: CONSTRUCT query
// ============================================================================
void testConstructQuery() {
    TEST("SPARQL parser CONSTRUCT query");

    SparqlParser parser;
    auto result = parser.parse(
        "CONSTRUCT { ?x <http://example.org/friend> ?y } "
        "WHERE { ?x <http://example.org/knows> ?y }"
    );

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(queryType(*result), SparqlQueryType::CONSTRUCT);
    auto& q = std::get<ConstructQuery>(*result);
    ASSERT_EQ(q.constructTemplate.size(), 1u);
    ASSERT_EQ(q.where.triples.size(), 1u);

    PASS();
}

// ============================================================================
// Test: DESCRIBE query
// ============================================================================
void testDescribeQuery() {
    TEST("SPARQL parser DESCRIBE query");

    SparqlParser parser;
    auto result = parser.parse(
        "DESCRIBE ?x WHERE { ?x <http://example.org/type> <http://example.org/Person> }"
    );

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(queryType(*result), SparqlQueryType::DESCRIBE);
    auto& q = std::get<DescribeQuery>(*result);
    ASSERT_EQ(q.variables.size(), 1u);

    PASS();
}

// ============================================================================
// Test: GROUP BY + HAVING
// ============================================================================
void testGroupByHaving() {
    TEST("SPARQL parser GROUP BY + HAVING");

    SparqlParser parser;
    // Note: parser only supports plain variable lists in SELECT, not
    // aggregate expressions like (COUNT(?x) AS ?cnt), so we use SELECT *
    auto result = parser.parse(
        "SELECT * "
        "WHERE { ?x <http://example.org/type> ?type } "
        "GROUP BY ?type HAVING(?type != <http://example.org/Cat>)"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_TRUE(!q.groupBy.empty());
    ASSERT_EQ(q.groupBy[0], "type");
    ASSERT_TRUE(q.having != nullptr);

    PASS();
}

// ============================================================================
// Test: ORDER BY + LIMIT + OFFSET
// ============================================================================
void testOrderByLimitOffset() {
    TEST("SPARQL parser ORDER BY + LIMIT + OFFSET");

    SparqlParser parser;
    auto result = parser.parse(
        "SELECT ?name WHERE { ?x <http://example.org/name> ?name } "
        "ORDER BY ?name LIMIT 10 OFFSET 5"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_TRUE(!q.orderBy.empty());
    ASSERT_EQ(q.orderBy[0].first, "name");
    ASSERT_EQ(q.limit, 10);
    ASSERT_EQ(q.offset, 5);

    PASS();
}

// ============================================================================
// Test: Prefix expansion (addPrefix + PREFIX in query)
// ============================================================================
void testPrefixExpansion() {
    TEST("SPARQL parser prefix expansion");

    SparqlParser parser;

    // PREFIX in query
    auto result = parser.parse(
        "PREFIX foaf: <http://xmlns.com/foaf/0.1/> "
        "SELECT ?name WHERE { ?x foaf:name ?name }"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_EQ(q.where.triples.size(), 1u);
    // The predicate should be expanded to the full IRI
    ASSERT_TRUE(q.where.triples[0].predicate.isIRI());
    ASSERT_EQ(q.where.triples[0].predicate.value, "http://xmlns.com/foaf/0.1/name");

    PASS();
}

// ============================================================================
// Test: addPrefix API
// ============================================================================
void testAddPrefix() {
    TEST("SPARQL parser addPrefix API");

    SparqlParser parser;
    parser.addPrefix("ex", "http://example.org/");

    auto result = parser.parse(
        "SELECT ?x WHERE { ?x ex:rel ?y }"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_EQ(q.where.triples.size(), 1u);
    ASSERT_TRUE(q.where.triples[0].predicate.isIRI());
    ASSERT_EQ(q.where.triples[0].predicate.value, "http://example.org/rel");

    PASS();
}

// ============================================================================
// Test: Parse error returns nullopt
// ============================================================================
void testParseError() {
    TEST("SPARQL parser error returns nullopt");

    SparqlParser parser;

    // Nonsensical input
    auto result = parser.parse("}} WHERE {{");
    ASSERT_TRUE(!result.has_value());
    ASSERT_TRUE(!parser.lastError().empty());

    PASS();
}

// ============================================================================
// Test: DISTINCT modifier
// ============================================================================
void testDistinct() {
    TEST("SPARQL parser DISTINCT modifier");

    SparqlParser parser;
    auto result = parser.parse(
        "SELECT DISTINCT ?name WHERE { ?x <http://example.org/name> ?name }"
    );

    ASSERT_TRUE(result.has_value());
    auto& q = std::get<SelectQuery>(*result);
    ASSERT_TRUE(q.distinct);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SPARQL Parser Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testSimpleSelect();
    testMultiplePatterns();
    testFilterExpressions();
    testOptional();
    testUnion();
    testAskQuery();
    testConstructQuery();
    testDescribeQuery();
    testGroupByHaving();
    testOrderByLimitOffset();
    testPrefixExpansion();
    testAddPrefix();
    testParseError();
    testDistinct();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
