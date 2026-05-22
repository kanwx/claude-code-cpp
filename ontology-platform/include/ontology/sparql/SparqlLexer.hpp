#pragma once

#include <ontology/Core.hpp>
#include <vector>
#include <string>
#include <cctype>

namespace ontology::sparql {

// ============================================================================
// SPARQL Token Types (plain enum for use in switch without static_cast)
// ============================================================================

enum TokenType {
    // Keywords
    SELECT, ASK, CONSTRUCT, DESCRIBE,
    WHERE, FILTER, OPTIONAL, UNION,
    ORDER, BY, ASC, DESC, LIMIT, OFFSET,
    GROUP, HAVING, DISTINCT, REDUCED,
    FROM, NAMED, USING, GRAPH,
    PREFIX, BASE,
    // Punctuation
    LBRACE, RBRACE,      // { }
    LPAREN, RPAREN,      // ( )
    LBRACKET, RBRACKET,  // [ ]
    DOT, COMMA, SEMICOLON,
    // Operators
    AND, OR, NOT,        // && || !
    EQ, NE, LT, GT, LE, GE,  // = != < > <= >=
    PLUS, MINUS, MUL, DIV,   // + - * /
    // Values
    IRI,                 // <http://...>
    PREFIXED_NAME,       // prefix:local
    VARIABLE,            // ?var or $var
    LITERAL,             // "value"
    INTEGER,             // 123
    DECIMAL,             // 123.45
    DOUBLE,              // 123.45e6
    BOOLEAN,             // true, false
    // Special
    A,                   // a (rdf:type)
    VALID_AT,            // VALID_AT clause
    VALID_BETWEEN,       // VALID_BETWEEN clause
    // Error
    Error,
    // End of input
    EOF_
};

// ============================================================================
// SPARQL Token
// ============================================================================

struct Token {
    TokenType type;
    String value;
    String datatype;
    String language;
    double numValue = 0.0;
    int intValue = 0;
    int line = 0;
    int column = 0;
};

// ============================================================================
// SPARQL Lexer
// ============================================================================

class SparqlLexer {
public:
    /// Tokenize a SPARQL query string into a vector of tokens.
    std::vector<Token> tokenize(const String& source);

private:
    String source_;
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;

    /// Peek at the current character without consuming it.
    char peek() const;

    /// Consume and return the current character, advancing position.
    char advance();

    /// Skip whitespace and # comments.
    void skipWhitespace();

    /// Skip a single # comment (called after '#' is detected).
    void skipComment();

    /// Scan the next token from the current position.
    Token scanToken();

    /// Scan a variable token (?var or $var).
    Token scanVariable();

    /// Scan an IRI token (<...>).
    Token scanIri();

    /// Scan a string literal ("...") with optional @lang or ^^<datatype>.
    Token scanString();

    /// Scan a number token (integer or decimal).
    Token scanNumber();

    /// Dispatch a word to keyword or PREFIXED_NAME.
    /// If the word is a known keyword, returns the keyword token.
    /// If followed by ':', reads the local part and returns PREFIXED_NAME.
    /// If not a keyword and not followed by ':', returns PREFIXED_NAME
    /// with a trailing colon (for function names like BOUND).
    Token scanKeywordOrPrefix(const String& word);
};

} // namespace ontology::sparql
