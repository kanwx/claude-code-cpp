#pragma once

#include "SparqlAst.hpp"
#include <ontology/Core.hpp>
#include <functional>

namespace ontology {

// Forward declarations needed for factory methods
class TripleStore;
class HybridStorage;

namespace sparql {

// ============================================================================
// SparqlExecutor — executes parsed SPARQL queries against a TripleSource
// ============================================================================

class SparqlExecutor {
public:
    // TripleSource: function returning triples matching a pattern.
    // Empty string for a position means wildcard.
    using TripleSource = std::function<std::vector<ontology::Triple>(
        const String& s, const String& p, const String& o)>;

    explicit SparqlExecutor(TripleSource source);

    /// Execute a parsed Query variant
    QueryResult execute(const Query& query);

    /// Parse and execute a SPARQL string
    QueryResult execute(const String& sparqlString);

    /// Enable/disable inference during execution
    void setEnableInference(bool enable);

    /// Set maximum inference chain depth
    void setMaxInferenceDepth(int depth);

    // Factory methods for common TripleSources
    static TripleSource fromTripleStore(ontology::TripleStore& store);
    static TripleSource fromHybridStorage(ontology::HybridStorage& storage);

private:
    TripleSource source_;
    bool enableInference_ = false;
    int maxInferenceDepth_ = 3;

    // Execution per query type
    QueryResult executeSelect(const SelectQuery& query);
    QueryResult executeAsk(const AskQuery& query);
    QueryResult executeConstruct(const ConstructQuery& query);
    QueryResult executeDescribe(const DescribeQuery& query);

    // Core pattern matching
    using Binding = std::unordered_map<String, Json>;
    std::vector<Binding> matchGroupPattern(const GroupPattern& pattern);
    std::vector<Binding> matchTriplePattern(const TriplePattern& tp, const Binding& initial);
    std::vector<Binding> matchOptional(const GroupPattern& optional, const std::vector<Binding>& bindings);
    std::vector<Binding> matchUnion(const std::vector<GroupPattern>& unions);

    // Join multiple triple patterns
    std::vector<Binding> joinPatterns(const std::vector<TriplePattern>& patterns,
                                       const Binding& initial = {});

    // Filter evaluation
    std::vector<Binding> applyFilters(const std::vector<Binding>& bindings,
                                       const std::vector<std::shared_ptr<FilterExpression>>& filters);

    // Aggregation
    std::vector<Binding> applyGroupBy(const std::vector<Binding>& bindings,
                                       const std::vector<String>& groupBy,
                                       const std::shared_ptr<FilterExpression>& having);

    // Sorting and pagination
    void applyOrderBy(std::vector<Binding>& bindings,
                      const std::vector<std::pair<String, bool>>& orderBy);
    void applyLimitOffset(std::vector<Binding>& bindings, int limit, int offset);

    // Property path evaluation
    std::vector<String> evaluatePropertyPath(const String& start, const String& path, int maxDepth = 10);

    // Temporal filtering
    std::vector<Binding> applyTemporalFilter(const GroupPattern& pattern,
                                              const std::vector<Binding>& bindings);
};

} // namespace sparql
} // namespace ontology
