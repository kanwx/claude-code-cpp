#pragma once

#include <ontology/Sparql.hpp>
#include <variant>

namespace ontology::sparql {

// ============================================================================
// Re-export existing types into the sparql namespace for convenience
// ============================================================================

using TriplePattern = ontology::TriplePattern;
using FilterExpression = ontology::FilterExpression;
using SparqlVariable = ontology::SparqlVariable;
using SparqlQueryType = ontology::SparqlQueryType;

// ============================================================================
// GroupPattern — a group graph pattern with triples, filters, optional, union
// ============================================================================

struct GroupPattern {
    std::vector<TriplePattern> triples;
    std::vector<std::shared_ptr<FilterExpression>> filters;
    std::vector<GroupPattern> optionalGroups;   // OPTIONAL { ... }
    std::vector<GroupPattern> unionGroups;       // { ... } UNION { ... }

    // Temporal clauses (from VALID_AT / VALID_BETWEEN)
    String validAtTimestamp;
    String validBetweenFrom;
    String validBetweenTo;

    // Get all variables referenced in this pattern
    std::vector<String> getVariables() const;
};

// ============================================================================
// SELECT query
// ============================================================================

struct SelectQuery {
    std::vector<SparqlVariable> variables;
    bool selectAll = false;                     // SELECT *
    bool distinct = false;                       // SELECT DISTINCT
    GroupPattern where;

    std::vector<std::pair<String, bool>> orderBy;  // (variable, ascending)
    int limit = -1;
    int offset = -1;
    std::vector<String> groupBy;
    std::shared_ptr<FilterExpression> having;

    std::unordered_map<String, String> prefixes;
};

// ============================================================================
// ASK query
// ============================================================================

struct AskQuery {
    GroupPattern where;
    std::unordered_map<String, String> prefixes;
};

// ============================================================================
// CONSTRUCT query
// ============================================================================

struct ConstructQuery {
    std::vector<TriplePattern> constructTemplate;
    GroupPattern where;
    std::unordered_map<String, String> prefixes;
};

// ============================================================================
// DESCRIBE query
// ============================================================================

struct DescribeQuery {
    std::vector<SparqlVariable> variables;      // or IRIs
    GroupPattern where;
    std::unordered_map<String, String> prefixes;
};

// ============================================================================
// Query variant — any SPARQL query type
// ============================================================================

using Query = std::variant<SelectQuery, AskQuery, ConstructQuery, DescribeQuery>;

// ============================================================================
// QueryResult — unified result type
// ============================================================================

struct QueryResult {
    SparqlQueryType queryType = SparqlQueryType::SELECT;

    // SELECT results
    std::vector<String> variables;
    std::vector<std::unordered_map<String, Json>> rows;

    // ASK result
    bool askResult = false;

    // CONSTRUCT results
    std::vector<TriplePattern> constructTriples;

    // Metadata
    int resultCount = 0;
    float executionTime = 0.0f;
    String errorMessage;

    bool success() const { return errorMessage.empty(); }

    Json toJson() const;
};

// Helper to get query type from the variant
SparqlQueryType queryType(const Query& q);

// Get all variables from a query
std::vector<String> allVariables(const Query& q);

} // namespace ontology::sparql
