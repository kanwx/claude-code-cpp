#include <ontology/Sparql.hpp>
#include <ontology/Temporal.hpp>
#include <spdlog/spdlog.h>
#include <regex>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <unordered_set>
#include <queue>

namespace ontology {

// ============================================================================
// SPARQL 解析器实现 (old ontology::SparqlParser — kept for backward compat)
// ============================================================================

SparqlParser::SparqlParser() {
    prefixes_["rdf"] = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    prefixes_["rdfs"] = "http://www.w3.org/2000/01/rdf-schema#";
    prefixes_["owl"] = "http://www.w3.org/2002/07/owl#";
    prefixes_["xsd"] = "http://www.w3.org/2001/XMLSchema#";
}

std::optional<SparqlQuery> SparqlParser::parse(const String& query) {
    auto tokens = tokenize(query);
    size_t pos = 0;

    try {
        return parseQuery(tokens, pos);
    } catch (const std::exception& e) {
        spdlog::error("SPARQL parse error: {}", e.what());
        return std::nullopt;
    }
}

void SparqlParser::addPrefix(const String& prefix, const String& iri) {
    prefixes_[prefix] = iri;
}

std::vector<SparqlParser::Token> SparqlParser::tokenize(const String& query) {
    std::vector<Token> tokens;
    size_t i = 0;

    auto skipWs = [&]() {
        while (i < query.size() && (std::isspace(query[i]) || query[i] == '#')) {
            if (query[i] == '#') {
                while (i < query.size() && query[i] != '\n') i++;
            }
            i++;
        }
    };

    auto readKeyword = [&]() -> String {
        String kw;
        while (i < query.size() && (std::isalnum(query[i]) || query[i] == '_')) {
            kw += query[i++];
        }
        return kw;
    };

    while (i < query.size()) {
        skipWs();
        if (i >= query.size()) break;

        char c = query[i];

        if (std::isalpha(c)) {
            String kw = readKeyword();
            std::transform(kw.begin(), kw.end(), kw.begin(), ::toupper);

            if (kw == "SELECT") tokens.push_back({Token::Type::SELECT, ""});
            else if (kw == "ASK") tokens.push_back({Token::Type::ASK, ""});
            else if (kw == "CONSTRUCT") tokens.push_back({Token::Type::CONSTRUCT, ""});
            else if (kw == "DESCRIBE") tokens.push_back({Token::Type::DESCRIBE, ""});
            else if (kw == "WHERE") tokens.push_back({Token::Type::WHERE, ""});
            else if (kw == "FILTER") tokens.push_back({Token::Type::FILTER, ""});
            else if (kw == "OPTIONAL") tokens.push_back({Token::Type::OPTIONAL, ""});
            else if (kw == "UNION") tokens.push_back({Token::Type::UNION, ""});
            else if (kw == "ORDER") tokens.push_back({Token::Type::ORDER, ""});
            else if (kw == "BY") tokens.push_back({Token::Type::BY, ""});
            else if (kw == "ASC") tokens.push_back({Token::Type::ASC, ""});
            else if (kw == "DESC") tokens.push_back({Token::Type::DESC, ""});
            else if (kw == "LIMIT") tokens.push_back({Token::Type::LIMIT, ""});
            else if (kw == "OFFSET") tokens.push_back({Token::Type::OFFSET, ""});
            else if (kw == "GROUP") tokens.push_back({Token::Type::GROUP, ""});
            else if (kw == "HAVING") tokens.push_back({Token::Type::HAVING, ""});
            else if (kw == "DISTINCT") tokens.push_back({Token::Type::DISTINCT, ""});
            else if (kw == "PREFIX") tokens.push_back({Token::Type::PREFIX, ""});
            else if (kw == "BASE") tokens.push_back({Token::Type::BASE, ""});
            else if (kw == "FROM") tokens.push_back({Token::Type::FROM, ""});
            else if (kw == "GRAPH") tokens.push_back({Token::Type::GRAPH, ""});
            else if (kw == "TRUE") {
                tokens.push_back({Token::Type::BOOLEAN, "true"});
            } else if (kw == "FALSE") {
                tokens.push_back({Token::Type::BOOLEAN, "false"});
            } else if (kw == "A") {
                tokens.push_back({Token::Type::A, "a"});
            } else if (kw == "VALID_AT") {
                tokens.push_back({Token::Type::VALID_AT, "VALID_AT"});
            } else if (kw == "VALID_BETWEEN") {
                tokens.push_back({Token::Type::VALID_BETWEEN, "VALID_BETWEEN"});
            } else if (kw == "NOT") {
                tokens.push_back({Token::Type::NOT, "NOT"});
            } else {
                tokens.push_back({Token::Type::PREFIXED_NAME, kw + ":"});
            }
            continue;
        }

        if (c == '?' || c == '$') {
            i++;
            String name;
            while (i < query.size() && (std::isalnum(query[i]) || query[i] == '_')) {
                name += query[i++];
            }
            tokens.push_back({Token::Type::VARIABLE, name});
            continue;
        }

        if (c == '<') {
            i++;
            String iri;
            while (i < query.size() && query[i] != '>') {
                iri += query[i++];
            }
            if (i < query.size()) i++;
            tokens.push_back({Token::Type::IRI, iri});
            continue;
        }

        if (c == '"') {
            i++;
            String lit;
            while (i < query.size() && query[i] != '"') {
                if (query[i] == '\\' && i + 1 < query.size()) {
                    i++;
                    lit += query[i];
                } else {
                    lit += query[i];
                }
                i++;
            }
            if (i < query.size()) i++;

            Token tok{Token::Type::LITERAL, lit};

            if (i < query.size() && query[i] == '@') {
                i++;
                String lang;
                while (i < query.size() && std::isalpha(query[i])) {
                    lang += query[i++];
                }
                tok.language = lang;
            } else if (i + 1 < query.size() && query[i] == '^' && query[i+1] == '^') {
                i += 2;
                if (query[i] == '<') {
                    i++;
                    String dt;
                    while (i < query.size() && query[i] != '>') {
                        dt += query[i++];
                    }
                    if (i < query.size()) i++;
                    tok.datatype = dt;
                }
            }

            tokens.push_back(tok);
            continue;
        }

        if (std::isdigit(c) || (c == '-' && i + 1 < query.size() && std::isdigit(query[i+1]))) {
            String num;
            if (c == '-') {
                num += c;
                i++;
            }
            while (i < query.size() && (std::isdigit(query[i]) || query[i] == '.' || query[i] == 'e' || query[i] == 'E')) {
                num += query[i++];
            }

            Token tok;
            if (num.find('.') != String::npos || num.find('e') != String::npos || num.find('E') != String::npos) {
                tok.type = Token::Type::DECIMAL;
                tok.numValue = std::stod(num);
            } else {
                tok.type = Token::Type::INTEGER;
                tok.intValue = std::stoi(num);
                tok.numValue = tok.intValue;
            }
            tok.value = num;
            tokens.push_back(tok);
            continue;
        }

        if (c == '{') { tokens.push_back({Token::Type::LBRACE, "{"}); i++; continue; }
        if (c == '}') { tokens.push_back({Token::Type::RBRACE, "}"}); i++; continue; }
        if (c == '(') { tokens.push_back({Token::Type::LPAREN, "("}); i++; continue; }
        if (c == ')') { tokens.push_back({Token::Type::RPAREN, ")"}); i++; continue; }
        if (c == '[') { tokens.push_back({Token::Type::LBRACKET, "["}); i++; continue; }
        if (c == ']') { tokens.push_back({Token::Type::RBRACKET, "]"}); i++; continue; }
        if (c == '.') { tokens.push_back({Token::Type::DOT, "."}); i++; continue; }
        if (c == ',') { tokens.push_back({Token::Type::COMMA, ","}); i++; continue; }
        if (c == ';') { tokens.push_back({Token::Type::SEMICOLON, ";"}); i++; continue; }

        if (c == '=') { tokens.push_back({Token::Type::EQ, "="}); i++; continue; }
        if (c == '!' && i + 1 < query.size() && query[i+1] == '=') {
            tokens.push_back({Token::Type::NE, "!="}); i += 2; continue;
        }
        if (c == '!') { tokens.push_back({Token::Type::NOT, "!"}); i++; continue; }
        if (c == '<' && i + 1 < query.size() && query[i+1] == '=') {
            tokens.push_back({Token::Type::LE, "<="}); i += 2; continue;
        }
        if (c == '<') { tokens.push_back({Token::Type::LT, "<"}); i++; continue; }
        if (c == '>' && i + 1 < query.size() && query[i+1] == '=') {
            tokens.push_back({Token::Type::GE, ">="}); i += 2; continue;
        }
        if (c == '>') { tokens.push_back({Token::Type::GT, ">"}); i++; continue; }
        if (c == '+') { tokens.push_back({Token::Type::PLUS, "+"}); i++; continue; }
        if (c == '-') { tokens.push_back({Token::Type::MINUS, "-"}); i++; continue; }
        if (c == '*') { tokens.push_back({Token::Type::MUL, "*"}); i++; continue; }
        if (c == '/') { tokens.push_back({Token::Type::DIV, "/"}); i++; continue; }
        if (c == '&' && i + 1 < query.size() && query[i+1] == '&') {
            tokens.push_back({Token::Type::AND, "&&"}); i += 2; continue;
        }
        if (c == '|' && i + 1 < query.size() && query[i+1] == '|') {
            tokens.push_back({Token::Type::OR, "||"}); i += 2; continue;
        }

        if (std::isalpha(c) || c == '_') {
            String name;
            while (i < query.size() && (std::isalnum(query[i]) || query[i] == '_')) {
                name += query[i++];
            }
            if (i < query.size() && query[i] == ':') {
                name += query[i++];
                while (i < query.size() && (std::isalnum(query[i]) || query[i] == '_' || query[i] == '-')) {
                    name += query[i++];
                }
                tokens.push_back({Token::Type::PREFIXED_NAME, name});
            } else {
                i -= name.length();
            }
            continue;
        }

        i++;
    }

    tokens.push_back({Token::Type::EOF_, ""});
    return tokens;
}

String SparqlParser::resolveIri(const String& prefixedName) {
    auto pos = prefixedName.find(':');
    if (pos == String::npos) return prefixedName;

    String prefix = prefixedName.substr(0, pos);
    String local = prefixedName.substr(pos + 1);

    auto it = prefixes_.find(prefix);
    if (it != prefixes_.end()) {
        return it->second + local;
    }

    return prefixedName;
}

SparqlQuery SparqlParser::parseQuery(std::vector<Token>& tokens, size_t& pos) {
    SparqlQuery query;

    while (tokens[pos].type == Token::Type::PREFIX) {
        pos++;
        String prefixName = tokens[pos].value;
        if (prefixName.back() == ':') prefixName.pop_back();
        pos++;
        String iri = tokens[pos].value;
        pos++;
        prefixes_[prefixName] = iri;
        query.prefixes[prefixName] = iri;
    }

    switch (tokens[pos].type) {
        case Token::Type::SELECT:
            query.type = SparqlQueryType::SELECT;
            pos++;
            parseSelectClause(tokens, pos, query);
            break;
        case Token::Type::ASK:
            query.type = SparqlQueryType::ASK;
            pos++;
            break;
        case Token::Type::CONSTRUCT:
            query.type = SparqlQueryType::CONSTRUCT;
            pos++;
            query.constructTemplate = parseGroupGraphPattern(tokens, pos, nullptr);
            break;
        case Token::Type::DESCRIBE:
            query.type = SparqlQueryType::DESCRIBE;
            pos++;
            break;
        default:
            throw std::runtime_error("Unknown query type");
    }

    if (tokens[pos].type == Token::Type::WHERE) {
        pos++;
    }

    if (tokens[pos].type == Token::Type::LBRACE) {
        query.patterns = parseGroupGraphPattern(tokens, pos, &query);
    }

    parseSolutionModifiers(tokens, pos, query);

    return query;
}

void SparqlParser::parseSelectClause(std::vector<Token>& tokens, size_t& pos, SparqlQuery& query) {
    if (tokens[pos].type == Token::Type::DISTINCT) {
        pos++;
    }

    if (tokens[pos].type == Token::Type::MUL) {
        query.selectAll = true;
        pos++;
    } else {
        while (tokens[pos].type == Token::Type::VARIABLE) {
            SparqlVariable var;
            var.name = tokens[pos].value;
            query.selectVariables.push_back(var);
            pos++;
        }
    }
}

std::vector<TriplePattern> SparqlParser::parseGroupGraphPattern(std::vector<Token>& tokens, size_t& pos, SparqlQuery* query) {
    std::vector<TriplePattern> patterns;

    if (tokens[pos].type != Token::Type::LBRACE) {
        return patterns;
    }
    pos++;

    while (tokens[pos].type != Token::Type::RBRACE && tokens[pos].type != Token::Type::EOF_) {
        if (tokens[pos].type == Token::Type::OPTIONAL) {
            pos++;
            auto optionalPatterns = parseGroupGraphPattern(tokens, pos, query);
            if (query) {
                query->optionals.push_back(optionalPatterns);
            }
            continue;
        }

        if (tokens[pos].type == Token::Type::FILTER) {
            pos++;
            auto filter = parseFilterExpression(tokens, pos);
            if (filter && query) {
                query->filters.push_back(filter);
            }
            continue;
        }

        if (tokens[pos].type == Token::Type::VALID_AT) {
            pos++;
            if (pos < tokens.size() && tokens[pos].type == Token::Type::LPAREN) {
                pos++;
                if (tokens[pos].type == Token::Type::VARIABLE ||
                    tokens[pos].type == Token::Type::IRI ||
                    tokens[pos].type == Token::Type::PREFIXED_NAME) {
                    patterns.push_back(parseTriplePattern(tokens, pos));
                }
                if (pos < tokens.size() && tokens[pos].type == Token::Type::COMMA) pos++;
                if (pos < tokens.size() && tokens[pos].type == Token::Type::LITERAL) {
                    if (query) query->validAtTimestamp = tokens[pos].value;
                    pos++;
                }
                if (pos < tokens.size() && tokens[pos].type == Token::Type::RPAREN) pos++;
                while (pos < tokens.size() && (tokens[pos].type == Token::Type::DOT ||
                       tokens[pos].type == Token::Type::SEMICOLON)) pos++;
            }
            continue;
        }

        if (tokens[pos].type == Token::Type::VALID_BETWEEN) {
            pos++;
            if (pos < tokens.size() && tokens[pos].type == Token::Type::LPAREN) {
                pos++;
                if (tokens[pos].type == Token::Type::VARIABLE ||
                    tokens[pos].type == Token::Type::IRI ||
                    tokens[pos].type == Token::Type::PREFIXED_NAME) {
                    patterns.push_back(parseTriplePattern(tokens, pos));
                }
                if (pos < tokens.size() && tokens[pos].type == Token::Type::COMMA) pos++;
                if (pos < tokens.size() && tokens[pos].type == Token::Type::LITERAL) {
                    if (query) query->validBetweenFrom = tokens[pos].value;
                    pos++;
                }
                if (pos < tokens.size() && tokens[pos].type == Token::Type::COMMA) pos++;
                if (pos < tokens.size() && tokens[pos].type == Token::Type::LITERAL) {
                    if (query) query->validBetweenTo = tokens[pos].value;
                    pos++;
                }
                if (pos < tokens.size() && tokens[pos].type == Token::Type::RPAREN) pos++;
                while (pos < tokens.size() && (tokens[pos].type == Token::Type::DOT ||
                       tokens[pos].type == Token::Type::SEMICOLON)) pos++;
            }
            continue;
        }

        if (tokens[pos].type == Token::Type::VARIABLE ||
            tokens[pos].type == Token::Type::IRI ||
            tokens[pos].type == Token::Type::PREFIXED_NAME) {
            patterns.push_back(parseTriplePattern(tokens, pos));
        } else {
            pos++;
        }
    }

    if (tokens[pos].type == Token::Type::RBRACE) pos++;

    while (tokens[pos].type == Token::Type::UNION) {
        pos++;
        auto unionPatterns = parseGroupGraphPattern(tokens, pos, query);
        if (query) {
            if (query->unions.empty()) {
                query->unions.push_back(patterns);
            }
            query->unions.push_back(unionPatterns);
        }
    }

    return patterns;
}

TriplePattern SparqlParser::parseTriplePattern(std::vector<Token>& tokens, size_t& pos) {
    TriplePattern pattern;
    pattern.subject = parseTerm(tokens, pos);
    pattern.predicate = parseTerm(tokens, pos);
    pattern.object = parseTerm(tokens, pos);
    while (tokens[pos].type == Token::Type::DOT ||
           tokens[pos].type == Token::Type::SEMICOLON ||
           tokens[pos].type == Token::Type::COMMA) {
        pos++;
    }
    return pattern;
}

TriplePattern::Term SparqlParser::parseTerm(std::vector<Token>& tokens, size_t& pos) {
    TriplePattern::Term term;
    const auto& tok = tokens[pos];

    switch (tok.type) {
        case Token::Type::VARIABLE:
            term.type = TriplePattern::TermType::Variable;
            term.value = tok.value;
            pos++;
            break;
        case Token::Type::IRI:
            term.type = TriplePattern::TermType::IRI;
            term.value = tok.value;
            pos++;
            break;
        case Token::Type::PREFIXED_NAME:
            term.type = TriplePattern::TermType::IRI;
            term.value = resolveIri(tok.value);
            pos++;
            break;
        case Token::Type::LITERAL:
            term.type = TriplePattern::TermType::Literal;
            term.value = tok.value;
            term.datatype = tok.datatype;
            term.language = tok.language;
            pos++;
            break;
        case Token::Type::A:
            term.type = TriplePattern::TermType::IRI;
            term.value = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
            pos++;
            break;
        default:
            throw std::runtime_error("Unexpected token in term");
    }

    return term;
}

std::shared_ptr<FilterExpression> SparqlParser::parseFilter(std::vector<Token>& tokens, size_t& pos) {
    return parseFilterExpression(tokens, pos);
}

std::shared_ptr<FilterExpression> SparqlParser::parseFilterExpression(std::vector<Token>& tokens, size_t& pos) {
    return parseOrExpression(tokens, pos);
}

std::shared_ptr<FilterExpression> SparqlParser::parseOrExpression(std::vector<Token>& tokens, size_t& pos) {
    auto left = parseAndExpression(tokens, pos);
    while (pos < tokens.size() && tokens[pos].type == Token::Type::OR) {
        pos++;
        auto right = parseAndExpression(tokens, pos);
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::OR;
        node->args = {left, right};
        left = node;
    }
    return left;
}

std::shared_ptr<FilterExpression> SparqlParser::parseAndExpression(std::vector<Token>& tokens, size_t& pos) {
    auto left = parseComparisonExpression(tokens, pos);
    while (pos < tokens.size() && tokens[pos].type == Token::Type::AND) {
        pos++;
        auto right = parseComparisonExpression(tokens, pos);
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::AND;
        node->args = {left, right};
        left = node;
    }
    return left;
}

std::shared_ptr<FilterExpression> SparqlParser::parseComparisonExpression(std::vector<Token>& tokens, size_t& pos) {
    auto left = parsePrimaryExpression(tokens, pos);
    if (pos >= tokens.size()) return left;

    auto tokType = tokens[pos].type;
    if (tokType == Token::Type::EQ || tokType == Token::Type::NE ||
        tokType == Token::Type::LT || tokType == Token::Type::GT ||
        tokType == Token::Type::LE || tokType == Token::Type::GE) {

        FilterExpression::Operator op;
        switch (tokType) {
            case Token::Type::EQ: op = FilterExpression::Operator::EQ; break;
            case Token::Type::NE: op = FilterExpression::Operator::NE; break;
            case Token::Type::LT: op = FilterExpression::Operator::LT; break;
            case Token::Type::GT: op = FilterExpression::Operator::GT; break;
            case Token::Type::LE: op = FilterExpression::Operator::LE; break;
            case Token::Type::GE: op = FilterExpression::Operator::GE; break;
            default: op = FilterExpression::Operator::EQ; break;
        }
        pos++;
        auto right = parsePrimaryExpression(tokens, pos);
        auto node = std::make_shared<FilterExpression>();
        node->op = op;
        node->args = {left, right};
        return node;
    }

    return left;
}

std::shared_ptr<FilterExpression> SparqlParser::parsePrimaryExpression(std::vector<Token>& tokens, size_t& pos) {
    if (pos >= tokens.size()) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = "null";
        return node;
    }

    const auto& tok = tokens[pos];

    if (tok.type == Token::Type::NOT) {
        pos++;
        auto operand = parsePrimaryExpression(tokens, pos);
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::NOT;
        node->args = {operand};
        return node;
    }

    if (tok.type == Token::Type::LPAREN) {
        pos++;
        auto expr = parseOrExpression(tokens, pos);
        if (pos < tokens.size() && tokens[pos].type == Token::Type::RPAREN) pos++;
        return expr;
    }

    if (tok.type == Token::Type::VARIABLE) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::VARIABLE;
        node->variableName = tok.value;
        pos++;
        return node;
    }

    if (tok.type == Token::Type::LITERAL) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        pos++;
        return node;
    }

    if (tok.type == Token::Type::INTEGER || tok.type == Token::Type::DECIMAL || tok.type == Token::Type::DOUBLE) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        pos++;
        return node;
    }

    if (tok.type == Token::Type::BOOLEAN) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        pos++;
        return node;
    }

    if (tok.type == Token::Type::IRI) {
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = tok.value;
        pos++;
        return node;
    }

    if (tok.type == Token::Type::PREFIXED_NAME) {
        String name = tok.value;
        if (!name.empty() && name.back() == ':') {
            name = name.substr(0, name.size() - 1);
        }
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);

        if (name == "BOUND" || name == "ISIRI" || name == "ISBLANK" ||
            name == "ISLITERAL" || name == "LANG" || name == "DATATYPE" ||
            name == "STR" || name == "STRLEN" || name == "UCASE" || name == "LCASE" ||
            name == "CONTAINS" || name == "STRSTARTS" || name == "STRENDS" ||
            name == "REGEX" || name == "SUBSTR") {
            pos++;
            return parseFunctionCall(name, tokens, pos);
        }
    }

    if (tok.type == Token::Type::PLUS) {
        pos++;
        return parsePrimaryExpression(tokens, pos);
    }
    if (tok.type == Token::Type::MINUS) {
        pos++;
        auto operand = parsePrimaryExpression(tokens, pos);
        auto zero = std::make_shared<FilterExpression>();
        zero->op = FilterExpression::Operator::LITERAL;
        zero->literalValue = "0";
        auto node = std::make_shared<FilterExpression>();
        node->op = FilterExpression::Operator::SUB;
        node->args = {zero, operand};
        return node;
    }

    pos++;
    auto node = std::make_shared<FilterExpression>();
    node->op = FilterExpression::Operator::LITERAL;
    node->literalValue = "";
    return node;
}

std::shared_ptr<FilterExpression> SparqlParser::parseFunctionCall(const String& funcName, std::vector<Token>& tokens, size_t& pos) {
    auto node = std::make_shared<FilterExpression>();

    std::unordered_map<String, FilterExpression::Operator> funcMap = {
        {"BOUND", FilterExpression::Operator::BOUND},
        {"ISIRI", FilterExpression::Operator::ISIRI},
        {"ISBLANK", FilterExpression::Operator::ISBLANK},
        {"ISLITERAL", FilterExpression::Operator::ISLITERAL},
        {"LANG", FilterExpression::Operator::LANG},
        {"DATATYPE", FilterExpression::Operator::DATATYPE},
        {"STR", FilterExpression::Operator::STR},
        {"STRLEN", FilterExpression::Operator::STRLEN},
        {"UCASE", FilterExpression::Operator::UCASE},
        {"LCASE", FilterExpression::Operator::LCASE},
        {"CONTAINS", FilterExpression::Operator::CONTAINS},
        {"STRSTARTS", FilterExpression::Operator::STRSTARTS},
        {"STRENDS", FilterExpression::Operator::STRENDS},
        {"REGEX", FilterExpression::Operator::REGEX},
        {"SUBSTR", FilterExpression::Operator::SUBSTR},
    };

    auto it = funcMap.find(funcName);
    if (it != funcMap.end()) {
        node->op = it->second;
    } else {
        node->op = FilterExpression::Operator::LITERAL;
        node->literalValue = funcName;
        return node;
    }

    if (pos < tokens.size() && tokens[pos].type == Token::Type::LPAREN) {
        pos++;
        while (pos < tokens.size() && tokens[pos].type != Token::Type::RPAREN) {
            if (tokens[pos].type == Token::Type::COMMA) {
                pos++;
                continue;
            }
            auto arg = parseOrExpression(tokens, pos);
            node->args.push_back(arg);
        }
        if (pos < tokens.size() && tokens[pos].type == Token::Type::RPAREN) pos++;
    }

    return node;
}

void SparqlParser::parseSolutionModifiers(std::vector<Token>& tokens, size_t& pos, SparqlQuery& query) {
    if (tokens[pos].type == Token::Type::GROUP) {
        pos++;
        if (tokens[pos].type == Token::Type::BY) pos++;
        while (tokens[pos].type == Token::Type::VARIABLE) {
            query.groupBy.push_back(tokens[pos].value);
            pos++;
        }
    }

    if (tokens[pos].type == Token::Type::HAVING) {
        pos++;
        auto having = parseFilterExpression(tokens, pos);
        if (having) query.having = having;
    }

    if (tokens[pos].type == Token::Type::ORDER) {
        pos++;
        if (tokens[pos].type == Token::Type::BY) pos++;
        while (tokens[pos].type == Token::Type::VARIABLE ||
               tokens[pos].type == Token::Type::ASC ||
               tokens[pos].type == Token::Type::DESC) {
            SparqlQuery::OrderSpec spec;
            if (tokens[pos].type == Token::Type::ASC) {
                spec.ascending = true;
                pos++;
            } else if (tokens[pos].type == Token::Type::DESC) {
                spec.ascending = false;
                pos++;
            }
            if (tokens[pos].type == Token::Type::VARIABLE) {
                spec.variable = tokens[pos].value;
                pos++;
            }
            query.orderBy.push_back(spec);
        }
    }

    if (tokens[pos].type == Token::Type::LIMIT) {
        pos++;
        query.limit = tokens[pos].intValue;
        pos++;
    }

    if (tokens[pos].type == Token::Type::OFFSET) {
        pos++;
        query.offset = tokens[pos].intValue;
        pos++;
    }
}

// ============================================================================
// SPARQL 执行器实现 (old ontology::SparqlExecutor — kept for backward compat)
// ============================================================================

SparqlExecutor::SparqlExecutor(StoragePtr storage) : storage_(storage) {}

SparqlResult SparqlExecutor::execute(const SparqlQuery& query) {
    auto start = std::chrono::high_resolution_clock::now();

    SparqlResult result;
    result.queryType = query.type;

    try {
        switch (query.type) {
            case SparqlQueryType::SELECT:
                result = executeSelect(query);
                break;
            case SparqlQueryType::ASK:
                result = executeAsk(query);
                break;
            case SparqlQueryType::CONSTRUCT:
                result = executeConstruct(query);
                break;
            case SparqlQueryType::DESCRIBE:
                result = executeDescribe(query);
                break;
        }
    } catch (const std::exception& e) {
        result.errorMessage = e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.executionTime = std::chrono::duration<float>(end - start).count();

    return result;
}

SparqlResult SparqlExecutor::execute(const String& queryString) {
    SparqlParser parser;
    auto query = parser.parse(queryString);
    if (!query) {
        SparqlResult result;
        result.errorMessage = "Failed to parse query";
        return result;
    }
    return execute(*query);
}

SparqlResult SparqlExecutor::executeSelect(const SparqlQuery& query) {
    SparqlResult result;
    result.queryType = SparqlQueryType::SELECT;
    result.variables = query.getAllVariables();

    if (!query.unions.empty()) {
        std::vector<std::unordered_map<String, Json>> allSolutions;
        std::unordered_set<String> seen;

        for (const auto& unionPatterns : query.unions) {
            auto solutions = matchPatterns(unionPatterns);
            for (const auto& sol : solutions) {
                String key;
                for (const auto& [k, v] : sol) {
                    key += k + "=" + v.dump() + ";";
                }
                if (seen.find(key) == seen.end()) {
                    seen.insert(key);
                    allSolutions.push_back(sol);
                }
            }
        }
        result.solutions = allSolutions;
    } else {
        result.solutions = matchPatterns(query.patterns);
    }

    // Temporal filtering
    if (!query.validAtTimestamp.empty()) {
        std::vector<std::unordered_map<String, Json>> temporalFiltered;
        for (const auto& binding : result.solutions) {
            bool valid = false;
            for (const auto& pattern : query.patterns) {
                String subj, pred, obj;
                if (pattern.subject.isVariable()) {
                    auto it = binding.find(pattern.subject.value);
                    subj = it != binding.end() ? it->second.get<String>() : "";
                } else {
                    subj = pattern.subject.value;
                }
                if (pattern.predicate.isVariable()) {
                    auto it = binding.find(pattern.predicate.value);
                    pred = it != binding.end() ? it->second.get<String>() : "";
                } else {
                    pred = pattern.predicate.value;
                }
                if (pattern.object.isVariable()) {
                    auto it = binding.find(pattern.object.value);
                    obj = it != binding.end() ? it->second.get<String>() : "";
                } else {
                    obj = pattern.object.value;
                }

                if (!subj.empty() && !pred.empty() && !obj.empty()) {
                    auto t = storage_->findTriple(subj, pred, obj);
                    if (t && isValidAt(t->validFrom, t->validTo, query.validAtTimestamp)) {
                        valid = true;
                        break;
                    }
                } else if (!subj.empty() && !pred.empty()) {
                    auto triples = storage_->findBySP(subj, pred);
                    for (const auto& tr : triples) {
                        if (isValidAt(tr.validFrom, tr.validTo, query.validAtTimestamp)) {
                            valid = true;
                            break;
                        }
                    }
                    if (valid) break;
                } else {
                    valid = true;
                    break;
                }
            }
            if (valid) temporalFiltered.push_back(binding);
        }
        result.solutions = std::move(temporalFiltered);
    } else if (!query.validBetweenFrom.empty() && !query.validBetweenTo.empty()) {
        int64_t fromMs = isoToEpochMs(query.validBetweenFrom);
        int64_t toMs = isoToEpochMs(query.validBetweenTo);

        std::vector<std::unordered_map<String, Json>> temporalFiltered;
        for (const auto& binding : result.solutions) {
            bool valid = false;
            for (const auto& pattern : query.patterns) {
                String subj, pred, obj;
                if (pattern.subject.isVariable()) {
                    auto it = binding.find(pattern.subject.value);
                    subj = it != binding.end() ? it->second.get<String>() : "";
                } else {
                    subj = pattern.subject.value;
                }
                if (pattern.predicate.isVariable()) {
                    auto it = binding.find(pattern.predicate.value);
                    pred = it != binding.end() ? it->second.get<String>() : "";
                } else {
                    pred = pattern.predicate.value;
                }
                if (pattern.object.isVariable()) {
                    auto it = binding.find(pattern.object.value);
                    obj = it != binding.end() ? it->second.get<String>() : "";
                } else {
                    obj = pattern.object.value;
                }

                auto overlaps = [&](const Triple& tr) -> bool {
                    if (tr.validFrom.empty() && tr.validTo.empty()) return true;
                    int64_t tFrom = tr.validFrom.empty() ? 0 : isoToEpochMs(tr.validFrom);
                    int64_t tTo = tr.validTo.empty() ? INT64_MAX : isoToEpochMs(tr.validTo);
                    return tFrom <= toMs && tTo >= fromMs;
                };

                if (!subj.empty() && !pred.empty() && !obj.empty()) {
                    auto t = storage_->findTriple(subj, pred, obj);
                    if (t && overlaps(*t)) { valid = true; break; }
                } else if (!subj.empty() && !pred.empty()) {
                    auto triples = storage_->findBySP(subj, pred);
                    for (const auto& tr : triples) {
                        if (overlaps(tr)) { valid = true; break; }
                    }
                    if (valid) break;
                } else {
                    valid = true;
                    break;
                }
            }
            if (valid) temporalFiltered.push_back(binding);
        }
        result.solutions = std::move(temporalFiltered);
    }

    if (!query.filters.empty()) {
        result.solutions = applyFilters(result.solutions, query.filters);
    }

    for (const auto& optionalPatterns : query.optionals) {
        result.solutions = applyOptional(result.solutions, optionalPatterns);
    }

    if (!query.groupBy.empty()) {
        result.solutions = applyGroupBy(result.solutions, query.groupBy, query.having);
        result.variables = query.groupBy;
        for (const auto& var : query.selectVariables) {
            if (std::find(query.groupBy.begin(), query.groupBy.end(), var.name) == query.groupBy.end()) {
                result.variables.push_back(var.name);
            }
        }
    }

    if (!query.orderBy.empty()) {
        applyOrderBy(result.solutions, query.orderBy);
    }

    if (query.offset > 0 && query.offset < (int)result.solutions.size()) {
        result.solutions.erase(result.solutions.begin(), result.solutions.begin() + query.offset);
    }
    if (query.limit > 0 && (int)result.solutions.size() > query.limit) {
        result.solutions.resize(query.limit);
    }

    result.resultCount = result.solutions.size();
    return result;
}

SparqlResult SparqlExecutor::executeAsk(const SparqlQuery& query) {
    SparqlResult result;
    result.queryType = SparqlQueryType::ASK;
    auto solutions = matchPatterns(query.patterns);
    result.askResult = !solutions.empty();
    return result;
}

SparqlResult SparqlExecutor::executeConstruct(const SparqlQuery& query) {
    SparqlResult result;
    result.queryType = SparqlQueryType::CONSTRUCT;

    auto solutions = matchPatterns(query.patterns);
    for (const auto& binding : solutions) {
        for (const auto& templatePattern : query.constructTemplate) {
            Triple triple;
            if (templatePattern.subject.isVariable()) {
                auto it = binding.find(templatePattern.subject.value);
                triple.subject = it != binding.end() ? it->second.get<String>() : "";
            } else {
                triple.subject = templatePattern.subject.value;
            }
            if (templatePattern.predicate.isVariable()) {
                auto it = binding.find(templatePattern.predicate.value);
                triple.predicate = it != binding.end() ? it->second.get<String>() : "";
            } else {
                triple.predicate = templatePattern.predicate.value;
            }
            if (templatePattern.object.isVariable()) {
                auto it = binding.find(templatePattern.object.value);
                if (it != binding.end()) {
                    triple.object = it->second.get<String>();
                    triple.isLiteral = it->second.is_string();
                }
            } else {
                triple.object = templatePattern.object.value;
                triple.isLiteral = templatePattern.object.isLiteral();
            }
            if (!triple.subject.empty() && !triple.predicate.empty() && !triple.object.empty()) {
                result.constructedTriples.push_back(triple);
            }
        }
    }

    result.resultCount = result.constructedTriples.size();
    return result;
}

SparqlResult SparqlExecutor::executeDescribe(const SparqlQuery& query) {
    SparqlResult result;
    result.queryType = SparqlQueryType::DESCRIBE;

    for (const auto& pattern : query.patterns) {
        if (!pattern.subject.isVariable()) {
            auto triples = storage_->findBySubject(pattern.subject.value);
            for (const auto& t : triples) result.constructedTriples.push_back(t);
        }
        if (!pattern.predicate.isVariable()) {
            auto triples = storage_->findByPredicate(pattern.predicate.value);
            for (const auto& t : triples) result.constructedTriples.push_back(t);
        }
        if (!pattern.object.isVariable()) {
            auto triples = storage_->findByObject(pattern.object.value);
            for (const auto& t : triples) result.constructedTriples.push_back(t);
        }
    }

    std::unordered_set<String> seen;
    std::vector<Triple> unique;
    for (const auto& t : result.constructedTriples) {
        String key = t.subject + "|" + t.predicate + "|" + t.object;
        if (seen.insert(key).second) unique.push_back(t);
    }
    result.constructedTriples = std::move(unique);
    result.resultCount = result.constructedTriples.size();
    return result;
}

std::vector<std::unordered_map<String, Json>> SparqlExecutor::matchPatterns(
    const std::vector<TriplePattern>& patterns,
    const std::unordered_map<String, Json>& initialBindings)
{
    std::vector<std::unordered_map<String, Json>> solutions;
    solutions.push_back(initialBindings);

    for (const auto& pattern : patterns) {
        std::vector<std::unordered_map<String, Json>> newSolutions;
        for (const auto& binding : solutions) {
            auto matches = matchPattern(pattern, binding);
            for (auto& match : matches) {
                auto merged = binding;
                for (const auto& [var, val] : match) merged[var] = val;
                newSolutions.push_back(merged);
            }
        }
        solutions = std::move(newSolutions);
        if (solutions.empty()) break;
    }

    return solutions;
}

std::vector<std::unordered_map<String, Json>> SparqlExecutor::matchPattern(
    const TriplePattern& pattern,
    const std::unordered_map<String, Json>& bindings)
{
    std::vector<std::unordered_map<String, Json>> results;
    String subject, predicate, object;

    if (pattern.subject.isVariable()) {
        auto it = bindings.find(pattern.subject.value);
        if (it != bindings.end()) subject = it->second.get<String>();
    } else {
        subject = pattern.subject.value;
    }
    if (pattern.predicate.isVariable()) {
        auto it = bindings.find(pattern.predicate.value);
        if (it != bindings.end()) predicate = it->second.get<String>();
    } else {
        predicate = pattern.predicate.value;
    }
    if (pattern.object.isVariable()) {
        auto it = bindings.find(pattern.object.value);
        if (it != bindings.end()) object = it->second.get<String>();
    } else {
        object = pattern.object.value;
    }

    std::vector<Triple> triples;
    if (!subject.empty() && !predicate.empty() && !object.empty()) {
        auto t = storage_->findTriple(subject, predicate, object);
        if (t) triples.push_back(*t);
    } else if (!subject.empty() && !predicate.empty()) {
        triples = storage_->findBySP(subject, predicate);
    } else if (!predicate.empty() && !object.empty()) {
        triples = storage_->findByPO(predicate, object);
    } else if (!subject.empty()) {
        triples = storage_->findBySubject(subject);
    } else if (!predicate.empty()) {
        triples = storage_->findByPredicate(predicate);
    } else {
        triples = storage_->getAllTriples();
    }

    for (const auto& triple : triples) {
        std::unordered_map<String, Json> binding;
        if (pattern.subject.isVariable() && subject.empty()) binding[pattern.subject.value] = triple.subject;
        if (pattern.predicate.isVariable() && predicate.empty()) binding[pattern.predicate.value] = triple.predicate;
        if (pattern.object.isVariable() && object.empty()) binding[pattern.object.value] = triple.object;
        if (!binding.empty()) results.push_back(binding);
    }

    return results;
}

std::vector<std::unordered_map<String, Json>> SparqlExecutor::applyFilters(
    const std::vector<std::unordered_map<String, Json>>& solutions,
    const std::vector<std::shared_ptr<FilterExpression>>& filters)
{
    std::vector<std::unordered_map<String, Json>> filtered;
    for (const auto& binding : solutions) {
        bool passes = true;
        for (const auto& filter : filters) {
            auto result = filter->evaluate(binding);
            if (!result || !result->get<bool>()) { passes = false; break; }
        }
        if (passes) filtered.push_back(binding);
    }
    return filtered;
}

std::vector<std::unordered_map<String, Json>> SparqlExecutor::applyOptional(
    const std::vector<std::unordered_map<String, Json>>& solutions,
    const std::vector<TriplePattern>& optionalPatterns)
{
    std::vector<std::unordered_map<String, Json>> result;
    for (const auto& binding : solutions) {
        auto optionalSolutions = matchPatterns(optionalPatterns, binding);
        if (!optionalSolutions.empty()) {
            for (const auto& optBinding : optionalSolutions) {
                auto merged = binding;
                for (const auto& [var, val] : optBinding) {
                    if (merged.find(var) == merged.end()) merged[var] = val;
                }
                result.push_back(merged);
            }
        } else {
            result.push_back(binding);
        }
    }
    return result;
}

void SparqlExecutor::applyOrderBy(
    std::vector<std::unordered_map<String, Json>>& solutions,
    const std::vector<SparqlQuery::OrderSpec>& orderBy)
{
    std::sort(solutions.begin(), solutions.end(), [&](const auto& a, const auto& b) {
        for (const auto& spec : orderBy) {
            auto itA = a.find(spec.variable);
            auto itB = b.find(spec.variable);
            if (itA == a.end() && itB == b.end()) continue;
            if (itA == a.end()) return spec.ascending;
            if (itB == b.end()) return !spec.ascending;
            auto cmp = itA->second.template get<String>().compare(itB->second.template get<String>());
            if (cmp != 0) return spec.ascending ? cmp < 0 : cmp > 0;
        }
        return false;
    });
}

void SparqlExecutor::applyLimitOffset(
    std::vector<std::unordered_map<String, Json>>& solutions,
    int limit, int offset)
{
    if (offset > 0 && offset < (int)solutions.size()) {
        solutions.erase(solutions.begin(), solutions.begin() + offset);
    }
    if (limit > 0 && (int)solutions.size() > limit) {
        solutions.resize(limit);
    }
}

std::vector<std::unordered_map<String, Json>> SparqlExecutor::applyGroupBy(
    const std::vector<std::unordered_map<String, Json>>& solutions,
    const std::vector<String>& groupBy,
    const std::shared_ptr<FilterExpression>& having)
{
    std::unordered_map<String, std::vector<std::unordered_map<String, Json>>> groups;
    for (const auto& solution : solutions) {
        std::ostringstream key;
        for (const auto& var : groupBy) {
            auto it = solution.find(var);
            if (it != solution.end()) key << it->second.dump() << "|";
            else key << "null|";
        }
        groups[key.str()].push_back(solution);
    }

    std::vector<std::unordered_map<String, Json>> result;
    for (const auto& [key, group] : groups) {
        if (group.empty()) continue;
        std::unordered_map<String, Json> groupedSolution;
        for (const auto& var : groupBy) {
            auto it = group[0].find(var);
            if (it != group[0].end()) groupedSolution[var] = it->second;
        }
        groupedSolution["__count__"] = Json((int)group.size());

        std::unordered_map<String, double> sums, mins, maxs;
        std::unordered_map<String, bool> numeric;
        for (const auto& sol : group) {
            for (const auto& [var, val] : sol) {
                if (val.is_number()) {
                    numeric[var] = true;
                    double v = val.get<double>();
                    sums[var] += v;
                    if (mins.find(var) == mins.end() || v < mins[var]) mins[var] = v;
                    if (maxs.find(var) == maxs.end() || v > maxs[var]) maxs[var] = v;
                }
            }
        }
        for (const auto& [var, isNum] : numeric) {
            if (isNum) {
                groupedSolution["__sum_" + var + "__"] = Json(sums[var]);
                groupedSolution["__avg_" + var + "__"] = Json(sums[var] / group.size());
                groupedSolution["__min_" + var + "__"] = Json(mins[var]);
                groupedSolution["__max_" + var + "__"] = Json(maxs[var]);
            }
        }

        if (having) {
            auto pass = having->evaluate(groupedSolution);
            if (!pass || !pass->get<bool>()) continue;
        }
        result.push_back(groupedSolution);
    }
    return result;
}

std::vector<String> SparqlExecutor::evaluatePropertyPath(
    const String& start, const String& path, int maxDepth)
{
    std::vector<String> results;
    std::unordered_set<String> visited;

    if (path.find('/') != String::npos) {
        std::vector<String> segments;
        std::istringstream iss(path);
        String segment;
        while (std::getline(iss, segment, '/')) segments.push_back(segment);
        std::vector<String> current = {start};
        for (const auto& seg : segments) {
            std::vector<String> next;
            for (const auto& node : current) {
                auto reachable = evaluatePropertyPath(node, seg, maxDepth);
                next.insert(next.end(), reachable.begin(), reachable.end());
            }
            current = std::move(next);
        }
        return current;
    }

    if (path.find('|') != String::npos) {
        std::vector<String> alternatives;
        std::istringstream iss(path);
        String alt;
        while (std::getline(iss, alt, '|')) alternatives.push_back(alt);
        for (const auto& a : alternatives) {
            auto reachable = evaluatePropertyPath(start, a, maxDepth);
            for (const auto& r : reachable) {
                if (visited.insert(r).second) results.push_back(r);
            }
        }
        return results;
    }

    bool transitive = false;
    bool includeSelf = false;
    String pred = path;

    if (path.size() >= 1 && path.back() == '+') {
        transitive = true;
        pred = path.substr(0, path.size() - 1);
    } else if (path.size() >= 1 && path.back() == '*') {
        transitive = true;
        includeSelf = true;
        pred = path.substr(0, path.size() - 1);
    }

    bool inverse = false;
    if (!pred.empty() && pred[0] == '^') {
        inverse = true;
        pred = pred.substr(1);
    }

    if (!transitive) {
        if (inverse) {
            auto triples = storage_->findByPO(pred, start);
            for (const auto& t : triples) results.push_back(t.subject);
        } else {
            auto triples = storage_->findBySP(start, pred);
            for (const auto& t : triples) results.push_back(t.object);
        }
    } else {
        std::queue<std::pair<String, int>> queue;
        queue.push({start, 0});
        visited.insert(start);
        if (includeSelf) results.push_back(start);

        while (!queue.empty()) {
            auto [node, depth] = queue.front();
            queue.pop();
            if (depth >= maxDepth) continue;

            std::vector<Triple> nextTriples;
            if (inverse) nextTriples = storage_->findByPO(pred, node);
            else nextTriples = storage_->findBySP(node, pred);

            for (const auto& t : nextTriples) {
                String next = inverse ? t.subject : t.object;
                if (visited.insert(next).second) {
                    results.push_back(next);
                    queue.push({next, depth + 1});
                }
            }
        }
    }

    return results;
}

// ============================================================================
// SPARQL 结果方法 (kept for backward compat)
// ============================================================================

Json SparqlResult::toJson() const {
    Json j;
    if (!errorMessage.empty()) { j["error"] = errorMessage; return j; }

    switch (queryType) {
        case SparqlQueryType::SELECT:
            j["type"] = "select";
            j["variables"] = variables;
            j["results"] = Json::array();
            for (const auto& solution : solutions) {
                Json row;
                for (const auto& [var, val] : solution) row[var] = val;
                j["results"].push_back(row);
            }
            break;
        case SparqlQueryType::ASK:
            j["type"] = "ask";
            j["result"] = askResult;
            break;
        case SparqlQueryType::CONSTRUCT:
            j["type"] = "construct";
            j["triples"] = Json::array();
            for (const auto& t : constructedTriples) {
                j["triples"].push_back({{"subject", t.subject}, {"predicate", t.predicate}, {"object", t.object}, {"isLiteral", t.isLiteral}});
            }
            break;
        case SparqlQueryType::DESCRIBE:
            j["type"] = "describe";
            j["triples"] = Json::array();
            for (const auto& t : constructedTriples) {
                j["triples"].push_back({{"subject", t.subject}, {"predicate", t.predicate}, {"object", t.object}, {"isLiteral", t.isLiteral}});
            }
            break;
    }
    j["count"] = resultCount;
    j["executionTime"] = executionTime;
    return j;
}

Json SparqlResult::toSparqlResultsJson() const {
    Json j;
    j["head"] = Json{{"vars", Json::array()}};
    for (const auto& var : variables) j["head"]["vars"].push_back(var);
    j["results"] = Json{{"bindings", Json::array()}};
    for (const auto& solution : solutions) {
        Json row;
        for (const auto& var : variables) {
            auto it = solution.find(var);
            if (it != solution.end()) row[var] = Json{{"value", it->second}};
        }
        j["results"]["bindings"].push_back(row);
    }
    return j;
}

String SparqlResult::toSparqlResultsXml() const {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\"?>\n";
    oss << "<sparql xmlns=\"http://www.w3.org/2005/sparql-results#\">\n";
    oss << "  <head>\n";
    for (const auto& var : variables) oss << "    <variable name=\"" << var << "\"/>\n";
    oss << "  </head>\n";

    if (queryType == SparqlQueryType::ASK) {
        oss << "  <boolean>" << (askResult ? "true" : "false") << "</boolean>\n";
    } else {
        oss << "  <results>\n";
        for (const auto& solution : solutions) {
            oss << "    <result>\n";
            for (const auto& var : variables) {
                auto it = solution.find(var);
                if (it != solution.end()) {
                    String value = it->second.is_string() ? it->second.get<String>() : it->second.dump();
                    oss << "      <binding name=\"" << var << "\"><literal>" << value << "</literal></binding>\n";
                }
            }
            oss << "    </result>\n";
        }
        oss << "  </results>\n";
    }
    oss << "</sparql>";
    return oss.str();
}

String SparqlResult::toCsv() const {
    std::ostringstream oss;
    for (size_t i = 0; i < variables.size(); ++i) {
        if (i > 0) oss << ",";
        oss << variables[i];
    }
    oss << "\n";
    for (const auto& solution : solutions) {
        for (size_t i = 0; i < variables.size(); ++i) {
            if (i > 0) oss << ",";
            auto it = solution.find(variables[i]);
            if (it != solution.end()) {
                String value = it->second.is_string() ? it->second.get<String>() : it->second.dump();
                if (value.find(',') != String::npos || value.find('"') != String::npos || value.find('\n') != String::npos) {
                    String escaped;
                    for (char c : value) { if (c == '"') escaped += "\"\""; else escaped += c; }
                    oss << "\"" << escaped << "\"";
                } else {
                    oss << value;
                }
            }
        }
        oss << "\n";
    }
    return oss.str();
}

String SparqlResult::toTsv() const {
    std::ostringstream oss;
    for (size_t i = 0; i < variables.size(); ++i) {
        if (i > 0) oss << "\t";
        oss << variables[i];
    }
    oss << "\n";
    for (const auto& solution : solutions) {
        for (size_t i = 0; i < variables.size(); ++i) {
            if (i > 0) oss << "\t";
            auto it = solution.find(variables[i]);
            if (it != solution.end()) {
                String value = it->second.is_string() ? it->second.get<String>() : it->second.dump();
                for (char& c : value) { if (c == '\t' || c == '\n') c = ' '; }
                oss << value;
            }
        }
        oss << "\n";
    }
    return oss.str();
}

// ============================================================================
// SparqlQuery methods (kept for backward compat)
// ============================================================================

String SparqlQuery::toString() const { return rawQuery; }

std::vector<String> SparqlQuery::getAllVariables() const {
    std::unordered_set<String> varSet;
    for (const auto& pattern : patterns) {
        auto vars = pattern.getVariables();
        varSet.insert(vars.begin(), vars.end());
    }
    for (const auto& var : selectVariables) varSet.insert(var.name);
    return std::vector<String>(varSet.begin(), varSet.end());
}

// ============================================================================
// FilterExpression 实现 (kept for backward compat — used by both old and new executors)
// ============================================================================

std::optional<Json> FilterExpression::evaluate(
    const std::unordered_map<String, Json>& bindings
) const {
    switch (op) {
        case Operator::VARIABLE: {
            auto it = bindings.find(variableName);
            return it != bindings.end() ? std::optional<Json>(it->second) : std::nullopt;
        }
        case Operator::LITERAL:
            return literalValue;
        case Operator::NOT: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val) return std::nullopt;
            return Json(!val->get<bool>());
        }
        case Operator::AND: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            return Json(a->get<bool>() && b->get<bool>());
        }
        case Operator::OR: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            return Json(a->get<bool>() || b->get<bool>());
        }
        case Operator::EQ: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            return Json(*a == *b);
        }
        case Operator::NE: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            return Json(*a != *b);
        }
        case Operator::BOUND: {
            if (args.size() != 1 || args[0]->op != Operator::VARIABLE) return std::nullopt;
            return Json(bindings.count(args[0]->variableName) > 0);
        }
        case Operator::LT: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) return Json(a->get<double>() < b->get<double>());
            return Json(a->get<String>() < b->get<String>());
        }
        case Operator::GT: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) return Json(a->get<double>() > b->get<double>());
            return Json(a->get<String>() > b->get<String>());
        }
        case Operator::LE: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) return Json(a->get<double>() <= b->get<double>());
            return Json(a->get<String>() <= b->get<String>());
        }
        case Operator::GE: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) return Json(a->get<double>() >= b->get<double>());
            return Json(a->get<String>() >= b->get<String>());
        }
        case Operator::ADD: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) return Json(a->get<double>() + b->get<double>());
            return std::nullopt;
        }
        case Operator::SUB: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) return Json(a->get<double>() - b->get<double>());
            return std::nullopt;
        }
        case Operator::MUL: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) return Json(a->get<double>() * b->get<double>());
            return std::nullopt;
        }
        case Operator::DIV: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b) return std::nullopt;
            if (a->is_number() && b->is_number()) {
                double bv = b->get<double>();
                if (bv == 0.0) return std::nullopt;
                return Json(a->get<double>() / bv);
            }
            return std::nullopt;
        }
        case Operator::STR: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val) return std::nullopt;
            return Json(val->is_string() ? val->get<String>() : val->dump());
        }
        case Operator::STRLEN: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val || !val->is_string()) return std::nullopt;
            return Json(static_cast<int>(val->get<String>().size()));
        }
        case Operator::UCASE: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val || !val->is_string()) return std::nullopt;
            String s = val->get<String>();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            return Json(s);
        }
        case Operator::LCASE: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val || !val->is_string()) return std::nullopt;
            String s = val->get<String>();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return Json(s);
        }
        case Operator::CONTAINS: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b || !a->is_string() || !b->is_string()) return std::nullopt;
            return Json(a->get<String>().find(b->get<String>()) != String::npos);
        }
        case Operator::STRSTARTS: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b || !a->is_string() || !b->is_string()) return std::nullopt;
            return Json(a->get<String>().find(b->get<String>()) == 0);
        }
        case Operator::STRENDS: {
            if (args.size() != 2) return std::nullopt;
            auto a = args[0]->evaluate(bindings);
            auto b = args[1]->evaluate(bindings);
            if (!a || !b || !a->is_string() || !b->is_string()) return std::nullopt;
            const String& str = a->get<String>();
            const String& suf = b->get<String>();
            if (suf.size() > str.size()) return Json(false);
            return Json(str.compare(str.size() - suf.size(), suf.size(), suf) == 0);
        }
        case Operator::REGEX: {
            if (args.size() < 2) return std::nullopt;
            auto text = args[0]->evaluate(bindings);
            auto pattern = args[1]->evaluate(bindings);
            if (!text || !pattern || !text->is_string() || !pattern->is_string()) return std::nullopt;
            try {
                std::regex re(pattern->get<String>());
                return Json(std::regex_search(text->get<String>(), re));
            } catch (const std::exception& e) {
                spdlog::error("SPARQL regex error: {}", e.what());
                return Json(false);
            }
        }
        case Operator::SUBSTR: {
            if (args.size() < 2 || args.size() > 3) return std::nullopt;
            auto str = args[0]->evaluate(bindings);
            auto start = args[1]->evaluate(bindings);
            if (!str || !start || !str->is_string() || !start->is_number()) return std::nullopt;
            int s = start->get<int>() - 1;
            if (s < 0) s = 0;
            const String& text = str->get<String>();
            if (args.size() == 3) {
                auto len = args[2]->evaluate(bindings);
                if (!len || !len->is_number()) return std::nullopt;
                int l = len->get<int>();
                if (s + l > static_cast<int>(text.size())) l = static_cast<int>(text.size()) - s;
                return Json(text.substr(s, l));
            }
            return Json(text.substr(s));
        }
        case Operator::ISIRI: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val) return Json(false);
            if (val->is_string()) {
                const String& s = val->get<String>();
                return Json(s.find("://") != String::npos || s.find("_:") != 0);
            }
            return Json(false);
        }
        case Operator::ISBLANK: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val || !val->is_string()) return Json(false);
            return Json(val->get<String>().find("_:") == 0);
        }
        case Operator::ISLITERAL: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val) return Json(false);
            return Json(val->is_string() || val->is_number() || val->is_boolean());
        }
        case Operator::LANG: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val) return std::nullopt;
            return Json("");
        }
        case Operator::DATATYPE: {
            if (args.size() != 1) return std::nullopt;
            auto val = args[0]->evaluate(bindings);
            if (!val) return std::nullopt;
            if (val->is_number_integer()) return Json("http://www.w3.org/2001/XMLSchema#integer");
            if (val->is_number_float()) return Json("http://www.w3.org/2001/XMLSchema#decimal");
            if (val->is_boolean()) return Json("http://www.w3.org/2001/XMLSchema#boolean");
            return Json("http://www.w3.org/2001/XMLSchema#string");
        }
        default:
            return std::nullopt;
    }
}

// ============================================================================
// SparqlUpdate toString (kept for backward compat)
// ============================================================================

String SparqlUpdate::toString() const {
    std::ostringstream oss;

    for (const auto& [prefix, iri] : prefixes) {
        oss << "PREFIX " << prefix << ": <" << iri << ">\n";
    }

    switch (type) {
        case SparqlUpdateType::InsertData:
            oss << "INSERT DATA {\n";
            for (const auto& p : data) oss << "  " << p.subject.value << " " << p.predicate.value << " " << p.object.value << " .\n";
            oss << "}";
            break;
        case SparqlUpdateType::DeleteData:
            oss << "DELETE DATA {\n";
            for (const auto& p : data) oss << "  " << p.subject.value << " " << p.predicate.value << " " << p.object.value << " .\n";
            oss << "}";
            break;
        case SparqlUpdateType::DeleteWhere:
            oss << "DELETE WHERE {\n";
            for (const auto& p : where) oss << "  " << p.subject.value << " " << p.predicate.value << " " << p.object.value << " .\n";
            oss << "}";
            break;
        case SparqlUpdateType::InsertDelete:
            if (!deleteTemplate.empty()) {
                oss << "DELETE {\n";
                for (const auto& p : deleteTemplate) oss << "  " << p.subject.value << " " << p.predicate.value << " " << p.object.value << " .\n";
                oss << "}\n";
            }
            if (!insertTemplate.empty()) {
                oss << "INSERT {\n";
                for (const auto& p : insertTemplate) oss << "  " << p.subject.value << " " << p.predicate.value << " " << p.object.value << " .\n";
                oss << "}\n";
            }
            if (!where.empty()) {
                oss << "WHERE {\n";
                for (const auto& p : where) oss << "  " << p.subject.value << " " << p.predicate.value << " " << p.object.value << " .\n";
                oss << "}";
            }
            break;
        case SparqlUpdateType::Clear:
            oss << "CLEAR ";
            if (silent) oss << "SILENT ";
            if (graph.empty()) oss << "DEFAULT";
            else oss << "GRAPH <" << graph << ">";
            break;
        case SparqlUpdateType::Drop:
            oss << "DROP ";
            if (silent) oss << "SILENT ";
            if (graph.empty()) oss << "DEFAULT";
            else oss << "GRAPH <" << graph << ">";
            break;
        case SparqlUpdateType::Create:
            oss << "CREATE ";
            if (silent) oss << "SILENT ";
            oss << "GRAPH <" << graph << ">";
            break;
        case SparqlUpdateType::Load:
            oss << "LOAD <" << sourceGraph << ">";
            if (!graph.empty()) oss << " INTO GRAPH <" << graph << ">";
            break;
        case SparqlUpdateType::Copy:
            oss << "COPY ";
            if (silent) oss << "SILENT ";
            oss << "GRAPH <" << sourceGraph << "> TO GRAPH <" << graph << ">";
            break;
        case SparqlUpdateType::Move:
            oss << "MOVE ";
            if (silent) oss << "SILENT ";
            oss << "GRAPH <" << sourceGraph << "> TO GRAPH <" << graph << ">";
            break;
        case SparqlUpdateType::Add:
            oss << "ADD ";
            if (silent) oss << "SILENT ";
            oss << "GRAPH <" << sourceGraph << "> TO GRAPH <" << graph << ">";
            break;
    }
    return oss.str();
}

// ============================================================================
// Old SparqlEndpoint implementation (kept for backward compat)
// ============================================================================

SparqlEndpoint::SparqlEndpoint(StoragePtr storage)
    : storage_(storage) {
    parser_ = std::make_unique<SparqlParser>();
    executor_ = std::make_unique<ontology::SparqlExecutor>(storage);
}

SparqlResult SparqlEndpoint::query(const String& queryString, const Json& options) {
    return executor_->execute(queryString);
}

bool SparqlEndpoint::update(const String& updateString) {
    auto updateOp = parseUpdate(updateString);
    if (!updateOp) return false;
    return executeUpdate(*updateOp);
}

std::optional<SparqlUpdate> SparqlEndpoint::parseUpdate(const String& updateString) {
    SparqlUpdate update;
    size_t pos = 0;
    String s = updateString;

    auto toUpper = [](const String& str) {
        String result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    };

    auto skipWs = [&]() { while (pos < s.size() && std::isspace(s[pos])) pos++; };
    auto readWord = [&]() -> String { skipWs(); String word; while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_')) word += s[pos++]; return word; };
    auto readIri = [&]() -> String { skipWs(); if (s[pos] == '<') { pos++; String iri; while (pos < s.size() && s[pos] != '>') iri += s[pos++]; if (pos < s.size()) pos++; return iri; } return ""; };
    auto readVariable = [&]() -> String { skipWs(); if (s[pos] == '?' || s[pos] == '$') { pos++; String var; while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_')) var += s[pos++]; return var; } return ""; };
    auto readPrefixedName = [&]() -> String { skipWs(); String name; while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_' || s[pos] == ':')) name += s[pos++]; return name; };
    auto readLiteral = [&]() -> String { skipWs(); if (s[pos] == '"') { pos++; String lit; while (pos < s.size() && s[pos] != '"') { if (s[pos] == '\\' && pos + 1 < s.size()) { pos++; switch (s[pos]) { case 'n': lit += '\n'; break; case 't': lit += '\t'; break; case '\\': lit += '\\'; break; case '"': lit += '"'; break; default: lit += s[pos]; } } else lit += s[pos]; pos++; } if (pos < s.size()) pos++; return lit; } return ""; };

    auto parseTriplePattern = [&]() -> std::vector<TriplePattern> {
        std::vector<TriplePattern> patterns;
        skipWs();
        if (pos < s.size() && s[pos] == '{') {
            pos++;
            while (pos < s.size() && s[pos] != '}') {
                skipWs();
                if (s[pos] == '}') break;
                TriplePattern pattern;
                if (s[pos] == '<') pattern.subject = TriplePattern::iri(readIri());
                else if (s[pos] == '?' || s[pos] == '$') pattern.subject = TriplePattern::variable(readVariable());
                else if (s[pos] == '_') { String bn; while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_' || s[pos] == ':')) bn += s[pos++]; pattern.subject = TriplePattern::blankNode(bn); }
                else pattern.subject = TriplePattern::iri(readPrefixedName());
                skipWs();
                if (s[pos] == '<') pattern.predicate = TriplePattern::iri(readIri());
                else if (s[pos] == '?' || s[pos] == '$') pattern.predicate = TriplePattern::variable(readVariable());
                else { String pred = readPrefixedName(); if (pred == "a") pred = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type"; pattern.predicate = TriplePattern::iri(pred); }
                skipWs();
                if (s[pos] == '<') pattern.object = TriplePattern::iri(readIri());
                else if (s[pos] == '?' || s[pos] == '$') pattern.object = TriplePattern::variable(readVariable());
                else if (s[pos] == '"') pattern.object = TriplePattern::literal(readLiteral());
                else if (s[pos] == '_') { String bn; while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_' || s[pos] == ':')) bn += s[pos++]; pattern.object = TriplePattern::blankNode(bn); }
                else pattern.object = TriplePattern::iri(readPrefixedName());
                patterns.push_back(pattern);
                skipWs();
                if (pos < s.size() && s[pos] == '.') pos++;
                else if (pos < s.size() && s[pos] == ';') pos++;
            }
            if (pos < s.size()) pos++;
        }
        return patterns;
    };

    while (pos < s.size()) {
        skipWs();
        String word = toUpper(readWord());
        if (word == "PREFIX") {
            skipWs();
            String prefix;
            while (pos < s.size() && s[pos] != ':') prefix += s[pos++];
            if (pos < s.size()) pos++;
            String iri = readIri();
            update.prefixes[prefix] = iri;
        } else { pos = 0; break; }
    }

    skipWs();
    String keyword = toUpper(readWord());

    if (keyword == "INSERT") {
        skipWs();
        String next = toUpper(readWord());
        if (next == "DATA") { update.type = SparqlUpdateType::InsertData; update.data = parseTriplePattern(); }
        else {
            update.type = SparqlUpdateType::InsertDelete;
            while (pos > 0 && s[pos-1] != '{') pos--;
            if (pos > 0) pos--;
            update.insertTemplate = parseTriplePattern();
            skipWs();
            String w = toUpper(readWord());
            if (w == "WHERE") update.where = parseTriplePattern();
        }
    } else if (keyword == "DELETE") {
        skipWs();
        String next = toUpper(readWord());
        if (next == "DATA") { update.type = SparqlUpdateType::DeleteData; update.data = parseTriplePattern(); }
        else if (next == "WHERE") { update.type = SparqlUpdateType::DeleteWhere; update.where = parseTriplePattern(); }
        else {
            update.type = SparqlUpdateType::InsertDelete;
            while (pos > 0 && s[pos-1] != '{') pos--;
            if (pos > 0) pos--;
            update.deleteTemplate = parseTriplePattern();
            skipWs();
            String ins = toUpper(readWord());
            if (ins == "INSERT") update.insertTemplate = parseTriplePattern();
            skipWs();
            String w = toUpper(readWord());
            if (w == "WHERE") update.where = parseTriplePattern();
        }
    } else if (keyword == "LOAD") {
        update.type = SparqlUpdateType::Load;
        skipWs();
        update.sourceGraph = readIri();
        skipWs();
        String into = toUpper(readWord());
        if (into == "INTO") { skipWs(); String g = toUpper(readWord()); if (g == "GRAPH") update.graph = readIri(); }
    } else if (keyword == "CLEAR") {
        update.type = SparqlUpdateType::Clear;
        skipWs();
        String g = toUpper(readWord());
        if (g == "GRAPH") update.graph = readIri();
    } else if (keyword == "DROP") {
        update.type = SparqlUpdateType::Drop;
        skipWs();
        String g = toUpper(readWord());
        if (g == "GRAPH") update.graph = readIri();
    } else if (keyword == "CREATE") {
        update.type = SparqlUpdateType::Create;
        skipWs();
        String g = toUpper(readWord());
        if (g == "GRAPH") update.graph = readIri();
    } else if (keyword == "COPY") {
        update.type = SparqlUpdateType::Copy;
        skipWs();
        String g1 = toUpper(readWord());
        if (g1 == "GRAPH") { update.sourceGraph = readIri(); skipWs(); String to = toUpper(readWord()); if (to == "TO") { skipWs(); String g2 = toUpper(readWord()); if (g2 == "GRAPH") update.graph = readIri(); } }
    } else {
        return std::nullopt;
    }

    return update;
}

bool SparqlEndpoint::executeUpdate(const SparqlUpdate& update) {
    if (!storage_) return false;

    auto resolveIri = [&](const String& name) -> String {
        if (name.find("://") != String::npos) return name;
        auto colonPos = name.find(':');
        if (colonPos != String::npos) {
            String prefix = name.substr(0, colonPos);
            String local = name.substr(colonPos + 1);
            auto it = update.prefixes.find(prefix);
            if (it != update.prefixes.end()) return it->second + local;
        }
        return name;
    };

    switch (update.type) {
        case SparqlUpdateType::InsertData: {
            for (const auto& pattern : update.data) {
                Triple t;
                t.subject = resolveIri(pattern.subject.value);
                t.predicate = resolveIri(pattern.predicate.value);
                t.object = resolveIri(pattern.object.value);
                storage_->addTriple(t);
            }
            return true;
        }
        case SparqlUpdateType::DeleteData: {
            for (const auto& pattern : update.data) {
                Triple t;
                t.subject = resolveIri(pattern.subject.value);
                t.predicate = resolveIri(pattern.predicate.value);
                t.object = resolveIri(pattern.object.value);
                storage_->removeTriple(t);
            }
            return true;
        }
        case SparqlUpdateType::DeleteWhere: {
            SparqlQuery query;
            query.type = SparqlQueryType::SELECT;
            query.patterns = update.where;
            auto result = executor_->execute(query);
            for (const auto& binding : result.solutions) {
                for (const auto& pattern : update.where) {
                    Triple t;
                    t.subject = pattern.subject.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.subject.value).get<String>() : resolveIri(pattern.subject.value);
                    t.predicate = pattern.predicate.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.predicate.value).get<String>() : resolveIri(pattern.predicate.value);
                    t.object = pattern.object.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.object.value).get<String>() : resolveIri(pattern.object.value);
                    storage_->removeTriple(t);
                }
            }
            return true;
        }
        case SparqlUpdateType::InsertDelete: {
            SparqlQuery query;
            query.type = SparqlQueryType::SELECT;
            query.patterns = update.where;
            auto result = executor_->execute(query);
            for (const auto& binding : result.solutions) {
                for (const auto& pattern : update.deleteTemplate) {
                    Triple t;
                    t.subject = pattern.subject.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.subject.value).get<String>() : resolveIri(pattern.subject.value);
                    t.predicate = pattern.predicate.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.predicate.value).get<String>() : resolveIri(pattern.predicate.value);
                    t.object = pattern.object.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.object.value).get<String>() : resolveIri(pattern.object.value);
                    storage_->removeTriple(t);
                }
            }
            for (const auto& binding : result.solutions) {
                for (const auto& pattern : update.insertTemplate) {
                    Triple t;
                    t.subject = pattern.subject.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.subject.value).get<String>() : resolveIri(pattern.subject.value);
                    t.predicate = pattern.predicate.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.predicate.value).get<String>() : resolveIri(pattern.predicate.value);
                    t.object = pattern.object.type == TriplePattern::TermType::Variable
                        ? binding.at(pattern.object.value).get<String>() : resolveIri(pattern.object.value);
                    storage_->addTriple(t);
                }
            }
            return true;
        }
        case SparqlUpdateType::Clear: {
            storage_->clear();
            return true;
        }
        case SparqlUpdateType::Drop:
        case SparqlUpdateType::Create:
        case SparqlUpdateType::Load:
        case SparqlUpdateType::Copy:
        case SparqlUpdateType::Move:
        case SparqlUpdateType::Add:
            return true;
    }
    return false;
}

Json SparqlEndpoint::getServiceDescription() const {
    return Json{
        {"type", "sparql"},
        {"version", "1.1"},
        {"features", {
            "SELECT", "ASK", "CONSTRUCT", "DESCRIBE",
            "FILTER", "OPTIONAL", "UNION",
            "ORDER BY", "LIMIT", "OFFSET", "GROUP BY", "HAVING",
            "VALID_AT", "VALID_BETWEEN",
            "INSERT DATA", "DELETE DATA", "DELETE WHERE",
            "INSERT-DELETE-WHERE", "LOAD", "CLEAR", "DROP", "CREATE",
            "COPY", "MOVE", "ADD"
        }}
    };
}

} // namespace ontology
