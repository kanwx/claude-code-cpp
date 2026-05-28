#include <ontology/sparql/SparqlParser.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>

namespace ontology::sparql {

// ============================================================================
// Constructor
// ============================================================================

SparqlParser::SparqlParser() {
    // Default prefixes (same as original)
    prefixes_["rdf"]  = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    prefixes_["rdfs"] = "http://www.w3.org/2000/01/rdf-schema#";
    prefixes_["owl"]  = "http://www.w3.org/2002/07/owl#";
    prefixes_["xsd"]  = "http://www.w3.org/2001/XMLSchema#";
}

// ============================================================================
// Public API
// ============================================================================

std::optional<Query> SparqlParser::parse(const String& source) {
    lastError_.clear();

    SparqlLexer lexer;
    tokens_ = lexer.tokenize(source);
    pos_ = 0;

    try {
        auto result = parseQuery();
        if (result) {
            return result;
        }
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("SPARQL parser error: {}", e.what());
        lastError_ = e.what();
        return std::nullopt;
    }
}

void SparqlParser::addPrefix(const String& prefix, const String& iri) {
    prefixes_[prefix] = iri;
}

void SparqlParser::clearPrefixes() {
    prefixes_.clear();
}

const String& SparqlParser::lastError() const {
    return lastError_;
}

// ============================================================================
// Peek / consume helpers
// ============================================================================

const Token& SparqlParser::peek() const {
    if (pos_ < tokens_.size()) {
        return tokens_[pos_];
    }
    static const Token eof{TokenType::EOF_, "", "", "", 0.0, 0, 0, 0};
    return eof;
}

Token SparqlParser::consume() {
    if (pos_ < tokens_.size()) {
        return tokens_[pos_++];
    }
    return Token{TokenType::EOF_, "", "", "", 0.0, 0, 0, 0};
}

bool SparqlParser::match(TokenType type) {
    if (check(type)) {
        consume();
        return true;
    }
    return false;
}

bool SparqlParser::check(TokenType type) const {
    return peek().type == type;
}

Token SparqlParser::expect(TokenType type, const String& msg) {
    if (check(type)) {
        return consume();
    }
    lastError_ = msg;
    throw std::runtime_error(msg);
}

// ============================================================================
// IRI expansion
// ============================================================================

String SparqlParser::expandPrefix(const String& prefixedName) {
    auto colonPos = prefixedName.find(':');
    if (colonPos == String::npos) return prefixedName;

    String prefix = prefixedName.substr(0, colonPos);
    String local  = prefixedName.substr(colonPos + 1);

    auto it = prefixes_.find(prefix);
    if (it != prefixes_.end()) {
        return it->second + local;
    }

    return prefixedName;
}

// ============================================================================
// Top-level query parser
// ============================================================================

std::optional<Query> SparqlParser::parseQuery() {
    // Parse PREFIX declarations
    while (check(TokenType::PREFIX)) {
        consume(); // skip PREFIX

        Token prefixTok = consume();
        String prefixName = prefixTok.value;
        // Remove trailing colon if present (lexer may include it)
        if (!prefixName.empty() && prefixName.back() == ':') {
            prefixName.pop_back();
        }

        Token iriTok = consume(); // <iri>
        String iri = iriTok.value;

        prefixes_[prefixName] = iri;
    }

    // Parse query form
    if (check(TokenType::SELECT)) {
        consume();
        auto query = parseSelect();
        query.prefixes = prefixes_;
        return query;
    }

    if (check(TokenType::ASK)) {
        consume();
        auto query = parseAsk();
        query.prefixes = prefixes_;
        return query;
    }

    if (check(TokenType::CONSTRUCT)) {
        consume();
        auto query = parseConstruct();
        query.prefixes = prefixes_;
        return query;
    }

    if (check(TokenType::DESCRIBE)) {
        consume();
        auto query = parseDescribe();
        query.prefixes = prefixes_;
        return query;
    }

    lastError_ = "Expected SELECT, ASK, CONSTRUCT, or DESCRIBE";
    return std::nullopt;
}

// ============================================================================
// SELECT
// ============================================================================

SelectQuery SparqlParser::parseSelect() {
    SelectQuery query;

    // DISTINCT?
    if (match(TokenType::DISTINCT)) {
        query.distinct = true;
    }

    // REDUCED? (treat same as DISTINCT for compatibility)
    match(TokenType::REDUCED);

    // * or variable list
    if (check(TokenType::MUL)) {
        query.selectAll = true;
        consume();
    } else {
        while (check(TokenType::VARIABLE)) {
            SparqlVariable var;
            var.name = consume().value;
            query.variables.push_back(var);
        }
    }

    // WHERE (optional keyword)
    match(TokenType::WHERE);

    // { group pattern }
    if (check(TokenType::LBRACE)) {
        query.where = parseGroupPattern();
    }

    // Solution modifiers
    parseSolutionModifiers(query);

    return query;
}

// ============================================================================
// ASK
// ============================================================================

AskQuery SparqlParser::parseAsk() {
    AskQuery query;

    // WHERE (optional keyword)
    match(TokenType::WHERE);

    // { group pattern }
    if (check(TokenType::LBRACE)) {
        query.where = parseGroupPattern();
    }

    return query;
}

// ============================================================================
// CONSTRUCT
// ============================================================================

ConstructQuery SparqlParser::parseConstruct() {
    ConstructQuery query;

    // Construct template: { triple patterns }
    if (check(TokenType::LBRACE)) {
        GroupPattern templateGroup = parseGroupPattern();
        query.constructTemplate = std::move(templateGroup.triples);
    }

    // WHERE (optional keyword)
    match(TokenType::WHERE);

    // { group pattern }
    if (check(TokenType::LBRACE)) {
        query.where = parseGroupPattern();
    }

    return query;
}

// ============================================================================
// DESCRIBE
// ============================================================================

DescribeQuery SparqlParser::parseDescribe() {
    DescribeQuery query;

    // Variable or IRI list
    while (check(TokenType::VARIABLE)) {
        SparqlVariable var;
        var.name = consume().value;
        query.variables.push_back(var);
    }

    // WHERE (optional keyword)
    match(TokenType::WHERE);

    // { group pattern }
    if (check(TokenType::LBRACE)) {
        query.where = parseGroupPattern();
    }

    return query;
}

// ============================================================================
// Group pattern parser
// ============================================================================

GroupPattern SparqlParser::parseGroupPattern() {
    GroupPattern group;

    if (!check(TokenType::LBRACE)) {
        return group;
    }
    consume(); // skip {

    while (!check(TokenType::RBRACE) && !check(TokenType::EOF_)) {
        // OPTIONAL { ... }
        if (check(TokenType::OPTIONAL)) {
            consume(); // skip OPTIONAL
            GroupPattern optionalGroup = parseGroupPattern();
            group.optionalGroups.push_back(std::move(optionalGroup));
            continue;
        }

        // FILTER ( expression )
        if (check(TokenType::FILTER)) {
            consume(); // skip FILTER
            auto filter = parseFilterExpr();
            if (filter) {
                group.filters.push_back(std::move(filter));
            }
            continue;
        }

        // VALID_AT(?s ?p ?o, "timestamp")
        if (check(TokenType::VALID_AT)) {
            consume(); // skip VALID_AT
            if (check(TokenType::LPAREN)) {
                consume(); // skip (

                // Parse triple pattern inside VALID_AT
                if (check(TokenType::VARIABLE) || check(TokenType::IRI) ||
                    check(TokenType::PREFIXED_NAME)) {
                    group.triples.push_back(parseTriplePattern());
                }

                // Skip comma
                match(TokenType::COMMA);

                // Parse timestamp literal
                if (check(TokenType::LITERAL)) {
                    group.validAtTimestamp = consume().value;
                }

                // Skip closing )
                match(TokenType::RPAREN);

                // Skip trailing dot or semicolon
                while (check(TokenType::DOT) || check(TokenType::SEMICOLON)) {
                    consume();
                }
            }
            continue;
        }

        // VALID_BETWEEN(?s ?p ?o, "from", "to")
        if (check(TokenType::VALID_BETWEEN)) {
            consume(); // skip VALID_BETWEEN
            if (check(TokenType::LPAREN)) {
                consume(); // skip (

                // Parse triple pattern inside VALID_BETWEEN
                if (check(TokenType::VARIABLE) || check(TokenType::IRI) ||
                    check(TokenType::PREFIXED_NAME)) {
                    group.triples.push_back(parseTriplePattern());
                }

                // Skip comma
                match(TokenType::COMMA);

                // Parse from timestamp
                if (check(TokenType::LITERAL)) {
                    group.validBetweenFrom = consume().value;
                }

                // Skip comma
                match(TokenType::COMMA);

                // Parse to timestamp
                if (check(TokenType::LITERAL)) {
                    group.validBetweenTo = consume().value;
                }

                // Skip closing )
                match(TokenType::RPAREN);

                // Skip trailing dot or semicolon
                while (check(TokenType::DOT) || check(TokenType::SEMICOLON)) {
                    consume();
                }
            }
            continue;
        }

        // Regular triple pattern
        if (check(TokenType::VARIABLE) || check(TokenType::IRI) ||
            check(TokenType::PREFIXED_NAME)) {
            group.triples.push_back(parseTriplePattern());
        } else {
            // Skip unknown token
            consume();
        }
    }

    // Skip closing }
    if (check(TokenType::RBRACE)) {
        consume();
    }

    // Check for UNION after closing brace
    while (check(TokenType::UNION)) {
        consume(); // skip UNION

        GroupPattern rightGroup = parseGroupPattern();

        // Create a left union group from the current triples
        GroupPattern leftGroup;
        leftGroup.triples = std::move(group.triples);
        leftGroup.filters = std::move(group.filters);
        leftGroup.optionalGroups = std::move(group.optionalGroups);
        leftGroup.validAtTimestamp = std::move(group.validAtTimestamp);
        leftGroup.validBetweenFrom = std::move(group.validBetweenFrom);
        leftGroup.validBetweenTo = std::move(group.validBetweenTo);

        // Clear current group's content (it's now in leftGroup)
        group.triples.clear();
        group.filters.clear();
        group.optionalGroups.clear();
        group.validAtTimestamp.clear();
        group.validBetweenFrom.clear();
        group.validBetweenTo.clear();

        // Push both into unionGroups
        group.unionGroups.push_back(std::move(leftGroup));
        group.unionGroups.push_back(std::move(rightGroup));
    }

    return group;
}

// ============================================================================
// Triple pattern parser
// ============================================================================

TriplePattern SparqlParser::parseTriplePattern() {
    TriplePattern pattern;

    // Subject
    pattern.subject = parseTerm();

    // Predicate
    pattern.predicate = parseTerm();

    // Object
    pattern.object = parseTerm();

    // Skip . or ; or ,
    while (check(TokenType::DOT) || check(TokenType::SEMICOLON) || check(TokenType::COMMA)) {
        consume();
    }

    return pattern;
}

// ============================================================================
// Term parser
// ============================================================================

TriplePattern::Term SparqlParser::parseTerm() {
    const Token& tok = peek();

    switch (tok.type) {
        case TokenType::VARIABLE:
            consume();
            return TriplePattern::variable(tok.value);

        case TokenType::IRI:
            consume();
            return TriplePattern::iri(tok.value);

        case TokenType::PREFIXED_NAME:
            consume();
            return TriplePattern::iri(expandPrefix(tok.value));

        case TokenType::LITERAL: {
            consume();
            TriplePattern::Term term = TriplePattern::literal(tok.value, tok.datatype, tok.language);
            return term;
        }

        case TokenType::INTEGER: {
            consume();
            // Represent integer as literal with xsd:integer datatype
            return TriplePattern::literal(tok.value,
                "http://www.w3.org/2001/XMLSchema#integer", "");
        }

        case TokenType::DECIMAL: {
            consume();
            return TriplePattern::literal(tok.value,
                "http://www.w3.org/2001/XMLSchema#decimal", "");
        }

        case TokenType::DOUBLE: {
            consume();
            return TriplePattern::literal(tok.value,
                "http://www.w3.org/2001/XMLSchema#double", "");
        }

        case TokenType::BOOLEAN: {
            consume();
            return TriplePattern::literal(tok.value,
                "http://www.w3.org/2001/XMLSchema#boolean", "");
        }

        case TokenType::A:
            consume();
            return TriplePattern::iri("http://www.w3.org/1999/02/22-rdf-syntax-ns#type");

        default:
            lastError_ = "Unexpected token in term: " + tok.value;
            throw std::runtime_error(lastError_);
    }
}

// ============================================================================
// FILTER expression parsers (precedence climbing)
// ============================================================================

std::shared_ptr<FilterExpression> SparqlParser::parseFilterExpr() {
    return parseOrExpr();
}

std::shared_ptr<FilterExpression> SparqlParser::parseOrExpr() {
    auto left = parseAndExpr();
    while (check(TokenType::OR)) {
        consume(); // skip ||
        auto right = parseAndExpr();
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::OR;
        node->args = {left, right};
        left = node;
    }
    return left;
}

std::shared_ptr<FilterExpression> SparqlParser::parseAndExpr() {
    auto left = parseComparisonExpr();
    while (check(TokenType::AND)) {
        consume(); // skip &&
        auto right = parseComparisonExpr();
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::AND;
        node->args = {left, right};
        left = node;
    }
    return left;
}

std::shared_ptr<FilterExpression> SparqlParser::parseComparisonExpr() {
    auto left = parsePrimaryExpr();

    if (check(TokenType::EQ) || check(TokenType::NE) ||
        check(TokenType::LT) || check(TokenType::GT) ||
        check(TokenType::LE) || check(TokenType::GE)) {

        auto tokType = peek().type;
        FilterExpression::Operator op;
        switch (tokType) {
            case TokenType::EQ: op = FilterExpression::Operator::EQ; break;
            case TokenType::NE: op = FilterExpression::Operator::NE; break;
            case TokenType::LT: op = FilterExpression::Operator::LT; break;
            case TokenType::GT: op = FilterExpression::Operator::GT; break;
            case TokenType::LE: op = FilterExpression::Operator::LE; break;
            case TokenType::GE: op = FilterExpression::Operator::GE; break;
            default: op = FilterExpression::Operator::EQ; break;
        }
        consume();
        auto right = parsePrimaryExpr();

        auto node = std::make_shared<FilterExpression>();
        node->op = op;
        node->args = {left, right};
        return node;
    }

    return left;
}

std::shared_ptr<FilterExpression> SparqlParser::parsePrimaryExpr() {
    if (check(TokenType::EOF_)) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = "null";
        return node;
    }

    const Token& tok = peek();

    // NOT (!)
    if (tok.type == TokenType::NOT) {
        consume();
        auto operand = parsePrimaryExpr();
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::NOT;
        node->args = {operand};
        return node;
    }

    // Parenthesized expression
    if (tok.type == TokenType::LPAREN) {
        consume(); // skip (
        auto expr = parseOrExpr();
        match(TokenType::RPAREN); // skip )
        return expr;
    }

    // Variable
    if (tok.type == TokenType::VARIABLE) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::VARIABLE;
        node->variableName = tok.value;
        consume();
        return node;
    }

    // Literal
    if (tok.type == TokenType::LITERAL) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        consume();
        return node;
    }

    // Number (integer, decimal, double)
    if (tok.type == TokenType::INTEGER || tok.type == TokenType::DECIMAL ||
        tok.type == TokenType::DOUBLE) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        consume();
        return node;
    }

    // Boolean
    if (tok.type == TokenType::BOOLEAN) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        consume();
        return node;
    }

    // IRI
    if (tok.type == TokenType::IRI) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        consume();
        return node;
    }

    // Built-in functions: check PREFIXED_NAME that might be a function call
    if (tok.type == TokenType::PREFIXED_NAME) {
        String name = tok.value;
        // Remove trailing colon from tokenizer function names
        if (!name.empty() && name.back() == ':') {
            name = name.substr(0, name.size() - 1);
        }
        String upperName = name;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

        // Known SPARQL filter functions
        if (upperName == "BOUND" || upperName == "ISIRI" || upperName == "ISBLANK" ||
            upperName == "ISLITERAL" || upperName == "LANG" || upperName == "DATATYPE" ||
            upperName == "STR" || upperName == "STRLEN" || upperName == "UCASE" ||
            upperName == "LCASE" || upperName == "CONTAINS" || upperName == "STRSTARTS" ||
            upperName == "STRENDS" || upperName == "REGEX" || upperName == "SUBSTR") {
            consume();
            return parseFunctionCall(upperName);
        }
    }

    // Arithmetic unary operators
    if (tok.type == TokenType::PLUS) {
        consume();
        return parsePrimaryExpr(); // unary + is identity
    }
    if (tok.type == TokenType::MINUS) {
        consume();
        auto operand = parsePrimaryExpr();
        // Negate: create 0 - operand
        auto zero = std::make_shared<FilterExpression>();
        zero->op = FilterExpression::Operator::LITERAL;
        zero->literalValue = "0";
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::SUB;
        node->args = {zero, operand};
        return node;
    }

    // Fallback: skip unknown token
    consume();
    auto node = std::make_shared<FilterExpression>();
    node->op = FilterExpression::Operator::LITERAL;
    node->literalValue = "";
    return node;
}

std::shared_ptr<FilterExpression> SparqlParser::parseFunctionCall(const String& funcName) {
    auto node = std::make_shared<FilterExpression>();

    // Map function name to operator
    static const std::unordered_map<String, FilterExpression::Operator> funcMap = {
        {"BOUND",      FilterExpression::Operator::BOUND},
        {"ISIRI",      FilterExpression::Operator::ISIRI},
        {"ISBLANK",    FilterExpression::Operator::ISBLANK},
        {"ISLITERAL",  FilterExpression::Operator::ISLITERAL},
        {"LANG",       FilterExpression::Operator::LANG},
        {"DATATYPE",   FilterExpression::Operator::DATATYPE},
        {"STR",        FilterExpression::Operator::STR},
        {"STRLEN",     FilterExpression::Operator::STRLEN},
        {"UCASE",      FilterExpression::Operator::UCASE},
        {"LCASE",      FilterExpression::Operator::LCASE},
        {"CONTAINS",   FilterExpression::Operator::CONTAINS},
        {"STRSTARTS",  FilterExpression::Operator::STRSTARTS},
        {"STRENDS",    FilterExpression::Operator::STRENDS},
        {"REGEX",      FilterExpression::Operator::REGEX},
        {"SUBSTR",     FilterExpression::Operator::SUBSTR},
    };

    auto it = funcMap.find(funcName);
    if (it != funcMap.end()) {
        node->op = it->second;
    } else {
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = funcName;
        return node;
    }

    // Parse arguments in parentheses
    if (check(TokenType::LPAREN)) {
        consume(); // skip (
        while (!check(TokenType::RPAREN) && !check(TokenType::EOF_)) {
            if (check(TokenType::COMMA)) {
                consume();
                continue;
            }
            auto arg = parseOrExpr();
            node->args.push_back(std::move(arg));
        }
        match(TokenType::RPAREN); // skip )
    }

    return node;
}

// ============================================================================
// Solution modifiers (GROUP BY, HAVING, ORDER BY, LIMIT, OFFSET)
// ============================================================================

void SparqlParser::parseSolutionModifiers(SelectQuery& query) {
    // GROUP BY
    if (check(TokenType::GROUP)) {
        consume(); // skip GROUP
        match(TokenType::BY); // skip BY

        while (check(TokenType::VARIABLE)) {
            query.groupBy.push_back(consume().value);
        }
    }

    // HAVING
    if (check(TokenType::HAVING)) {
        consume(); // skip HAVING
        auto having = parseFilterExpr();
        if (having) {
            query.having = std::move(having);
        }
    }

    // ORDER BY
    if (check(TokenType::ORDER)) {
        consume(); // skip ORDER
        match(TokenType::BY); // skip BY

        while (check(TokenType::VARIABLE) || check(TokenType::ASC) || check(TokenType::DESC)) {
            bool ascending = true;

            if (check(TokenType::ASC)) {
                ascending = true;
                consume();
            } else if (check(TokenType::DESC)) {
                ascending = false;
                consume();
            }

            String varName;
            if (check(TokenType::VARIABLE)) {
                varName = consume().value;
            }

            query.orderBy.emplace_back(varName, ascending);
        }
    }

    // LIMIT
    if (check(TokenType::LIMIT)) {
        consume(); // skip LIMIT
        if (check(TokenType::INTEGER)) {
            query.limit = consume().intValue;
        }
    }

    // OFFSET
    if (check(TokenType::OFFSET)) {
        consume(); // skip OFFSET
        if (check(TokenType::INTEGER)) {
            query.offset = consume().intValue;
        }
    }
}

} // namespace ontology::sparql
