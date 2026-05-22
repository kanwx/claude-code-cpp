// include/ontology/sparql/SparqlParser.hpp
#pragma once

#include "SparqlLexer.hpp"
#include "SparqlAst.hpp"
#include <optional>

namespace ontology::sparql {

class SparqlParser {
public:
    SparqlParser();

    /// Parse a SPARQL query string into a Query variant.
    /// Returns nullopt on parse error.
    std::optional<Query> parse(const String& source);

    /// Set a prefix mapping (e.g., "foaf" -> "http://xmlns.com/foaf/0.1/")
    void addPrefix(const String& prefix, const String& iri);

    /// Clear all prefix mappings
    void clearPrefixes();

    /// Get the last error message (set when parse returns nullopt)
    const String& lastError() const;

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    std::unordered_map<String, String> prefixes_;
    String lastError_;

    // Peek/consume helpers
    const Token& peek() const;
    Token consume();
    bool match(TokenType type);
    bool check(TokenType type) const;
    Token expect(TokenType type, const String& msg);

    // Query type parsers
    std::optional<Query> parseQuery();
    SelectQuery parseSelect();
    AskQuery parseAsk();
    ConstructQuery parseConstruct();
    DescribeQuery parseDescribe();

    // Pattern parsers
    GroupPattern parseGroupPattern();
    TriplePattern parseTriplePattern();
    TriplePattern::Term parseTerm();

    // Filter expression parsers
    std::shared_ptr<FilterExpression> parseFilterExpr();
    std::shared_ptr<FilterExpression> parseOrExpr();
    std::shared_ptr<FilterExpression> parseAndExpr();
    std::shared_ptr<FilterExpression> parseComparisonExpr();
    std::shared_ptr<FilterExpression> parsePrimaryExpr();
    std::shared_ptr<FilterExpression> parseFunctionCall(const String& funcName);

    // Solution modifiers
    void parseSolutionModifiers(SelectQuery& query);

    // IRI expansion
    String expandPrefix(const String& prefixed);
};

} // namespace ontology::sparql
