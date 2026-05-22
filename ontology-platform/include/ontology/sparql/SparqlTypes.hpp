#pragma once

// ============================================================================
// SparqlTypes.hpp — Shared SPARQL type definitions used by both the old
// ontology:: types and the new sparql:: types.
//
// This header breaks the circular dependency between Sparql.hpp and
// sparql/SparqlAst.hpp. It contains the fundamental type definitions
// (TriplePattern, FilterExpression, SparqlVariable, SparqlQueryType)
// that both namespaces need.
// ============================================================================

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <nlohmann/json.hpp>
#include <ontology/Core.hpp>

namespace ontology {

using Json = nlohmann::json;
using String = std::string;

// ============================================================================
// SPARQL query type
// ============================================================================

enum class SparqlQueryType {
    SELECT,
    ASK,
    CONSTRUCT,
    DESCRIBE
};

// ============================================================================
// SPARQL variable
// ============================================================================

struct SparqlVariable {
    String name;
    bool isAnonymous = false;

    String toString() const {
        return isAnonymous ? "_" : "?" + name;
    }
};

// ============================================================================
// SPARQL triple pattern
// ============================================================================

struct TriplePattern {
    enum class TermType { Variable, IRI, Literal, BlankNode };

    struct Term {
        TermType type;
        String value;
        String datatype;
        String language;

        bool isVariable() const { return type == TermType::Variable; }
        bool isIRI() const { return type == TermType::IRI; }
        bool isLiteral() const { return type == TermType::Literal; }

        String toString() const {
            switch (type) {
                case TermType::Variable: return "?" + value;
                case TermType::IRI: return "<" + value + ">";
                case TermType::Literal: return "\"" + value + "\"";
                case TermType::BlankNode: return "_:" + value;
            }
            return value;
        }
    };

    static Term variable(const String& v) { return Term{TermType::Variable, v}; }
    static Term iri(const String& v) { return Term{TermType::IRI, v}; }
    static Term literal(const String& v, const String& dt = "", const String& lang = "") {
        return Term{TermType::Literal, v, dt, lang};
    }
    static Term blankNode(const String& v) { return Term{TermType::BlankNode, v}; }

    Term subject;
    Term predicate;
    Term object;

    String toString() const {
        return subject.toString() + " " + predicate.toString() + " " + object.toString() + " .";
    }

    std::vector<String> getVariables() const {
        std::vector<String> vars;
        if (subject.isVariable()) vars.push_back(subject.value);
        if (predicate.isVariable()) vars.push_back(predicate.value);
        if (object.isVariable()) vars.push_back(object.value);
        return vars;
    }
};

// ============================================================================
// SPARQL FILTER expression
// ============================================================================

struct FilterExpression {
    enum class Operator {
        AND, OR, NOT,
        EQ, NE, LT, GT, LE, GE,
        STR, STRLEN, UCASE, LCASE, CONTAINS, STRSTARTS, STRENDS,
        REGEX, SUBSTR,
        ADD, SUB, MUL, DIV,
        BOUND, ISIRI, ISBLANK, ISLITERAL, LANG, DATATYPE,
        LITERAL, VARIABLE
    };

    Operator op;
    std::vector<std::shared_ptr<FilterExpression>> args;
    String literalValue;
    String variableName;

    std::optional<Json> evaluate(
        const std::unordered_map<String, Json>& bindings
    ) const;
};

} // namespace ontology
