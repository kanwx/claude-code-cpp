#include <ontology/sparql/SparqlLexer.hpp>
#include <algorithm>
#include <cstdlib>

namespace ontology::sparql {

// ============================================================================
// SparqlLexer implementation
// ============================================================================

char SparqlLexer::peek() const {
    if (pos_ >= source_.size()) return '\0';
    return source_[pos_];
}

char SparqlLexer::advance() {
    if (pos_ >= source_.size()) return '\0';
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

void SparqlLexer::skipComment() {
    while (pos_ < source_.size() && source_[pos_] != '\n') {
        pos_++;
    }
}

void SparqlLexer::skipWhitespace() {
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (c == '\n') {
                line_++;
                column_ = 1;
            } else {
                column_++;
            }
            pos_++;
        } else if (c == '#') {
            skipComment();
        } else {
            break;
        }
    }
}

Token SparqlLexer::scanVariable() {
    Token tok;
    tok.type = VARIABLE;
    tok.line = line_;
    tok.column = column_;

    advance(); // consume '?' or '$'

    String name;
    while (pos_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
        name += advance();
    }
    tok.value = name;
    return tok;
}

Token SparqlLexer::scanIri() {
    Token tok;
    tok.type = IRI;
    tok.line = line_;
    tok.column = column_;

    advance(); // consume '<'

    String iri;
    while (pos_ < source_.size() && source_[pos_] != '>') {
        iri += advance();
    }
    if (pos_ < source_.size()) {
        advance(); // consume '>'
    }
    tok.value = iri;
    return tok;
}

Token SparqlLexer::scanString() {
    Token tok;
    tok.type = LITERAL;
    tok.line = line_;
    tok.column = column_;

    advance(); // consume opening '"'

    String lit;
    while (pos_ < source_.size() && source_[pos_] != '"') {
        if (source_[pos_] == '\\' && pos_ + 1 < source_.size()) {
            advance(); // consume backslash
            lit += advance(); // consume escaped char
        } else {
            lit += advance();
        }
    }
    if (pos_ < source_.size()) {
        advance(); // consume closing '"'
    }

    tok.value = lit;

    // Check for language tag @lang
    if (pos_ < source_.size() && source_[pos_] == '@') {
        advance(); // consume '@'
        String lang;
        while (pos_ < source_.size() && std::isalpha(static_cast<unsigned char>(source_[pos_]))) {
            lang += advance();
        }
        // Handle language subtags like en-US
        while (pos_ < source_.size() && source_[pos_] == '-') {
            lang += advance();
            while (pos_ < source_.size() && std::isalpha(static_cast<unsigned char>(source_[pos_]))) {
                lang += advance();
            }
        }
        tok.language = lang;
    }
    // Check for datatype ^^<...>
    else if (pos_ + 1 < source_.size() && source_[pos_] == '^' && source_[pos_ + 1] == '^') {
        advance(); // consume first '^'
        advance(); // consume second '^'
        if (pos_ < source_.size() && source_[pos_] == '<') {
            advance(); // consume '<'
            String dt;
            while (pos_ < source_.size() && source_[pos_] != '>') {
                dt += advance();
            }
            if (pos_ < source_.size()) {
                advance(); // consume '>'
            }
            tok.datatype = dt;
        }
    }

    return tok;
}

Token SparqlLexer::scanNumber() {
    Token tok;
    tok.line = line_;
    tok.column = column_;

    String num;
    while (pos_ < source_.size() && (std::isdigit(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '.' || source_[pos_] == 'e' || source_[pos_] == 'E')) {
        num += advance();
    }

    if (num.find('.') != String::npos || num.find('e') != String::npos || num.find('E') != String::npos) {
        tok.type = DECIMAL;
        tok.numValue = std::stod(num);
    } else {
        tok.type = INTEGER;
        tok.intValue = std::stoi(num);
        tok.numValue = tok.intValue;
    }
    tok.value = num;
    return tok;
}

Token SparqlLexer::scanKeywordOrPrefix(const String& word) {
    String upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    Token tok;
    tok.line = line_;
    tok.column = column_ - static_cast<int>(word.size());

    // If followed by ':', this is a PREFIXED_NAME (prefix:local).
    // This consolidates the original's two separate alphabetic paths:
    //   - lines 67-116: keyword recognition
    //   - lines 246-262: prefix name recognition (UNREACHABLE in original)
    // By checking for ':' here, both cases are handled in one place.
    if (pos_ < source_.size() && source_[pos_] == ':') {
        advance(); // consume ':'
        String local;
        while (pos_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_' || source_[pos_] == '-')) {
            local += advance();
        }
        tok.type = PREFIXED_NAME;
        tok.value = word + ":" + local;
        return tok;
    }

    // Keyword dispatch
    if (upper == "SELECT")       { tok.type = SELECT;       tok.value = "";            return tok; }
    if (upper == "ASK")         { tok.type = ASK;          tok.value = "";            return tok; }
    if (upper == "CONSTRUCT")   { tok.type = CONSTRUCT;    tok.value = "";            return tok; }
    if (upper == "DESCRIBE")    { tok.type = DESCRIBE;     tok.value = "";            return tok; }
    if (upper == "WHERE")       { tok.type = WHERE;        tok.value = "";            return tok; }
    if (upper == "FILTER")      { tok.type = FILTER;       tok.value = "";            return tok; }
    if (upper == "OPTIONAL")    { tok.type = OPTIONAL;     tok.value = "";            return tok; }
    if (upper == "UNION")       { tok.type = UNION;        tok.value = "";            return tok; }
    if (upper == "ORDER")       { tok.type = ORDER;        tok.value = "";            return tok; }
    if (upper == "BY")          { tok.type = BY;           tok.value = "";            return tok; }
    if (upper == "ASC")         { tok.type = ASC;          tok.value = "";            return tok; }
    if (upper == "DESC")        { tok.type = DESC;         tok.value = "";            return tok; }
    if (upper == "LIMIT")       { tok.type = LIMIT;        tok.value = "";            return tok; }
    if (upper == "OFFSET")      { tok.type = OFFSET;       tok.value = "";            return tok; }
    if (upper == "GROUP")       { tok.type = GROUP;        tok.value = "";            return tok; }
    if (upper == "HAVING")      { tok.type = HAVING;       tok.value = "";            return tok; }
    if (upper == "DISTINCT")    { tok.type = DISTINCT;     tok.value = "";            return tok; }
    if (upper == "REDUCED")     { tok.type = REDUCED;      tok.value = "";            return tok; }
    if (upper == "PREFIX")      { tok.type = PREFIX;       tok.value = "";            return tok; }
    if (upper == "BASE")        { tok.type = BASE;         tok.value = "";            return tok; }
    if (upper == "FROM")        { tok.type = FROM;         tok.value = "";            return tok; }
    if (upper == "NAMED")       { tok.type = NAMED;        tok.value = "";            return tok; }
    if (upper == "USING")       { tok.type = USING;        tok.value = "";            return tok; }
    if (upper == "GRAPH")       { tok.type = GRAPH;        tok.value = "";            return tok; }
    if (upper == "TRUE")        { tok.type = BOOLEAN;      tok.value = "true";        return tok; }
    if (upper == "FALSE")       { tok.type = BOOLEAN;      tok.value = "false";       return tok; }
    if (upper == "A")           { tok.type = A;            tok.value = "a";           return tok; }
    if (upper == "VALID_AT")    { tok.type = VALID_AT;     tok.value = "VALID_AT";    return tok; }
    if (upper == "VALID_BETWEEN") { tok.type = VALID_BETWEEN; tok.value = "VALID_BETWEEN"; return tok; }
    if (upper == "NOT")         { tok.type = NOT;          tok.value = "NOT";         return tok; }

    // Not a keyword and not followed by ':' — treat as PREFIXED_NAME with trailing colon.
    // This matches the original behavior for BOUND and other unknown words.
    tok.type = PREFIXED_NAME;
    tok.value = word + ":";
    return tok;
}

Token SparqlLexer::scanToken() {
    skipWhitespace();
    if (pos_ >= source_.size()) {
        Token tok;
        tok.type = EOF_;
        tok.value = "";
        tok.line = line_;
        tok.column = column_;
        return tok;
    }

    char c = peek();
    int startLine = line_;
    int startCol = column_;

    // Alphabetic: keyword or prefix name (consolidates original's two alphabetic paths)
    if (std::isalpha(static_cast<unsigned char>(c))) {
        String word;
        while (pos_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
            word += advance();
        }
        return scanKeywordOrPrefix(word);
    }

    // Underscore start: prefix name like _:blank or _prefix:local
    if (c == '_') {
        String word;
        word += advance();
        while (pos_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
            word += advance();
        }
        if (pos_ < source_.size() && source_[pos_] == ':') {
            advance();
            String local;
            while (pos_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_' || source_[pos_] == '-')) {
                local += advance();
            }
            Token tok;
            tok.type = PREFIXED_NAME;
            tok.value = word + ":" + local;
            tok.line = startLine;
            tok.column = startCol;
            return tok;
        }
        Token tok;
        tok.type = PREFIXED_NAME;
        tok.value = word + ":";
        tok.line = startLine;
        tok.column = startCol;
        return tok;
    }

    // Variable ?var or $var
    if (c == '?' || c == '$') {
        return scanVariable();
    }

    // '<' handling: must distinguish between IRI (<http://...>) and operators (<=, <).
    // Check <= first (LE), then check if next char looks like IRI content, else LT.
    if (c == '<') {
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            advance(); advance();
            Token tok;
            tok.type = LE;
            tok.value = "<=";
            tok.line = startLine;
            tok.column = startCol;
            return tok;
        }
        // If next char is not whitespace/EOF, treat as IRI start
        if (pos_ + 1 < source_.size() && !std::isspace(static_cast<unsigned char>(source_[pos_ + 1]))) {
            return scanIri();
        }
        // Standalone '<' (e.g., in FILTER ?x < 5)
        advance();
        Token tok;
        tok.type = LT;
        tok.value = "<";
        tok.line = startLine;
        tok.column = startCol;
        return tok;
    }

    // String literal "..."
    if (c == '"') {
        return scanString();
    }

    // Number (digits only; '-' is always MINUS operator)
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return scanNumber();
    }

    // Punctuation
    if (c == '{') { advance(); Token tok; tok.type = LBRACE;    tok.value = "{"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '}') { advance(); Token tok; tok.type = RBRACE;    tok.value = "}"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '(') { advance(); Token tok; tok.type = LPAREN;    tok.value = "("; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == ')') { advance(); Token tok; tok.type = RPAREN;    tok.value = ")"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '[') { advance(); Token tok; tok.type = LBRACKET;  tok.value = "["; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == ']') { advance(); Token tok; tok.type = RBRACKET;  tok.value = "]"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '.') { advance(); Token tok; tok.type = DOT;       tok.value = "."; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == ',') { advance(); Token tok; tok.type = COMMA;     tok.value = ","; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == ';') { advance(); Token tok; tok.type = SEMICOLON; tok.value = ";"; tok.line = startLine; tok.column = startCol; return tok; }

    // Operators
    if (c == '=') { advance(); Token tok; tok.type = EQ; tok.value = "="; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '!' && pos_ < source_.size() && source_[pos_] == '=') {
        advance(); advance();
        Token tok; tok.type = NE; tok.value = "!="; tok.line = startLine; tok.column = startCol; return tok;
    }
    if (c == '!') { advance(); Token tok; tok.type = NOT; tok.value = "!"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '>' && pos_ < source_.size() && source_[pos_] == '=') {
        advance(); advance();
        Token tok; tok.type = GE; tok.value = ">="; tok.line = startLine; tok.column = startCol; return tok;
    }
    if (c == '>') { advance(); Token tok; tok.type = GT; tok.value = ">"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '+') { advance(); Token tok; tok.type = PLUS;  tok.value = "+"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '-') { advance(); Token tok; tok.type = MINUS; tok.value = "-"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '*') { advance(); Token tok; tok.type = MUL;   tok.value = "*"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '/') { advance(); Token tok; tok.type = DIV;   tok.value = "/"; tok.line = startLine; tok.column = startCol; return tok; }
    if (c == '&' && pos_ < source_.size() && source_[pos_] == '&') {
        advance(); advance();
        Token tok; tok.type = AND; tok.value = "&&"; tok.line = startLine; tok.column = startCol; return tok;
    }
    if (c == '|' && pos_ < source_.size() && source_[pos_] == '|') {
        advance(); advance();
        Token tok; tok.type = OR; tok.value = "||"; tok.line = startLine; tok.column = startCol; return tok;
    }

    // Unknown character — emit Error token
    advance();
    Token tok;
    tok.type = Error;
    tok.value = String(1, c);
    tok.line = startLine;
    tok.column = startCol;
    return tok;
}

std::vector<Token> SparqlLexer::tokenize(const String& source) {
    source_ = source;
    pos_ = 0;
    line_ = 1;
    column_ = 1;

    std::vector<Token> tokens;

    while (true) {
        Token tok = scanToken();
        if (tok.type == EOF_) {
            tok.line = line_;
            tok.column = column_;
            tokens.push_back(tok);
            break;
        }
        tokens.push_back(tok);
    }

    return tokens;
}

} // namespace ontology::sparql
