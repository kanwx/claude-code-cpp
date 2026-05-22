#include "TestUtils.hpp"
#include <ontology/sparql/SparqlLexer.hpp>

using namespace ontology;
using namespace ontology::sparql;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Test: Keywords recognized
// ============================================================================
void testKeywords() {
    TEST("SPARQL lexer keywords");

    SparqlLexer lexer;

    // SELECT — keyword tokens have type set; value may be empty per implementation
    {
        auto tokens = lexer.tokenize("SELECT");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, SELECT);
    }

    // WHERE
    {
        auto tokens = lexer.tokenize("WHERE");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, WHERE);
    }

    // FILTER
    {
        auto tokens = lexer.tokenize("FILTER");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, FILTER);
    }

    // OPTIONAL
    {
        auto tokens = lexer.tokenize("OPTIONAL");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, OPTIONAL);
    }

    // UNION
    {
        auto tokens = lexer.tokenize("UNION");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, UNION);
    }

    // ASK, CONSTRUCT, DESCRIBE
    {
        auto tokens = lexer.tokenize("ASK");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, ASK);
    }
    {
        auto tokens = lexer.tokenize("CONSTRUCT");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, CONSTRUCT);
    }
    {
        auto tokens = lexer.tokenize("DESCRIBE");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, DESCRIBE);
    }

    // DISTINCT, ORDER, BY, LIMIT, OFFSET, GROUP, HAVING
    {
        auto tokens = lexer.tokenize("DISTINCT");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, DISTINCT);
    }
    {
        auto tokens = lexer.tokenize("ORDER");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, ORDER);
    }
    {
        auto tokens = lexer.tokenize("BY");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, BY);
    }
    {
        auto tokens = lexer.tokenize("LIMIT");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, LIMIT);
    }
    {
        auto tokens = lexer.tokenize("OFFSET");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, OFFSET);
    }
    {
        auto tokens = lexer.tokenize("GROUP");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, GROUP);
    }
    {
        auto tokens = lexer.tokenize("HAVING");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, HAVING);
    }

    PASS();
}

// ============================================================================
// Test: Variables (?x, ?subject, $var)
// ============================================================================
void testVariables() {
    TEST("SPARQL lexer variables");

    SparqlLexer lexer;

    // ?x
    {
        auto tokens = lexer.tokenize("?x");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, VARIABLE);
        ASSERT_EQ(tokens[0].value, "x");
    }

    // ?subject
    {
        auto tokens = lexer.tokenize("?subject");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, VARIABLE);
        ASSERT_EQ(tokens[0].value, "subject");
    }

    // $var
    {
        auto tokens = lexer.tokenize("$var");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, VARIABLE);
        ASSERT_EQ(tokens[0].value, "var");
    }

    PASS();
}

// ============================================================================
// Test: IRIs (<http://example.org/x>)
// ============================================================================
void testIRIs() {
    TEST("SPARQL lexer IRIs");

    SparqlLexer lexer;

    {
        auto tokens = lexer.tokenize("<http://example.org/x>");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, IRI);
        ASSERT_EQ(tokens[0].value, "http://example.org/x");
    }

    {
        auto tokens = lexer.tokenize("<urn:isbn:123>");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, IRI);
        ASSERT_EQ(tokens[0].value, "urn:isbn:123");
    }

    PASS();
}

// ============================================================================
// Test: String literals ("hello", "hello"@en, "42"^^<xsd:integer>)
// ============================================================================
void testStringLiterals() {
    TEST("SPARQL lexer string literals");

    SparqlLexer lexer;

    // Plain string
    {
        auto tokens = lexer.tokenize("\"hello\"");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, LITERAL);
        ASSERT_EQ(tokens[0].value, "hello");
    }

    // Language-tagged
    {
        auto tokens = lexer.tokenize("\"hello\"@en");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, LITERAL);
        ASSERT_EQ(tokens[0].value, "hello");
        ASSERT_EQ(tokens[0].language, "en");
    }

    // Datatyped
    {
        auto tokens = lexer.tokenize("\"42\"^^<xsd:integer>");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, LITERAL);
        ASSERT_EQ(tokens[0].value, "42");
        ASSERT_EQ(tokens[0].datatype, "xsd:integer");
    }

    PASS();
}

// ============================================================================
// Test: Numbers (42 -> INTEGER, 3.14 -> DECIMAL, 1.5e6 -> DOUBLE)
// ============================================================================
void testNumbers() {
    TEST("SPARQL lexer numbers");

    SparqlLexer lexer;

    // Integer
    {
        auto tokens = lexer.tokenize("42");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, INTEGER);
        ASSERT_EQ(tokens[0].intValue, 42);
    }

    // Decimal
    {
        auto tokens = lexer.tokenize("3.14");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, DECIMAL);
    }

    // Double (scientific notation)
    {
        auto tokens = lexer.tokenize("1.5e6");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, DOUBLE);
    }

    PASS();
}

// ============================================================================
// Test: Punctuation ({, }, (, ), .)
// ============================================================================
void testPunctuation() {
    TEST("SPARQL lexer punctuation");

    SparqlLexer lexer;

    {
        auto tokens = lexer.tokenize("{ } ( ) . , ; [ ]");
        // Expect: LBRACE RBRACE LPAREN RPAREN DOT COMMA SEMICOLON LBRACKET RBRACKET EOF
        ASSERT_TRUE(tokens.size() >= 10);
        ASSERT_EQ(tokens[0].type, LBRACE);
        ASSERT_EQ(tokens[1].type, RBRACE);
        ASSERT_EQ(tokens[2].type, LPAREN);
        ASSERT_EQ(tokens[3].type, RPAREN);
        ASSERT_EQ(tokens[4].type, DOT);
        ASSERT_EQ(tokens[5].type, COMMA);
        ASSERT_EQ(tokens[6].type, SEMICOLON);
        ASSERT_EQ(tokens[7].type, LBRACKET);
        ASSERT_EQ(tokens[8].type, RBRACKET);
    }

    PASS();
}

// ============================================================================
// Test: Operators (=, !=, <, >, <=, >=)
// ============================================================================
void testOperators() {
    TEST("SPARQL lexer operators");

    SparqlLexer lexer;

    // Comparison operators (note: < and > are also used for IRIs,
    // so we test in non-IRI context)
    {
        auto tokens = lexer.tokenize("= != <= >=");
        ASSERT_TRUE(tokens.size() >= 4);
        ASSERT_EQ(tokens[0].type, EQ);
        ASSERT_EQ(tokens[1].type, NE);
        ASSERT_EQ(tokens[2].type, LE);
        ASSERT_EQ(tokens[3].type, GE);
    }

    // Logical operators
    {
        auto tokens = lexer.tokenize("&& || !");
        ASSERT_TRUE(tokens.size() >= 3);
        ASSERT_EQ(tokens[0].type, AND);
        ASSERT_EQ(tokens[1].type, OR);
        ASSERT_EQ(tokens[2].type, NOT);
    }

    // Arithmetic operators
    {
        auto tokens = lexer.tokenize("+ - * /");
        ASSERT_TRUE(tokens.size() >= 4);
        ASSERT_EQ(tokens[0].type, PLUS);
        ASSERT_EQ(tokens[1].type, MINUS);
        ASSERT_EQ(tokens[2].type, MUL);
        ASSERT_EQ(tokens[3].type, DIV);
    }

    PASS();
}

// ============================================================================
// Test: Boolean (true, false)
// ============================================================================
void testBooleans() {
    TEST("SPARQL lexer booleans");

    SparqlLexer lexer;

    {
        auto tokens = lexer.tokenize("true");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, BOOLEAN);
        ASSERT_EQ(tokens[0].value, "true");
    }

    {
        auto tokens = lexer.tokenize("false");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, BOOLEAN);
        ASSERT_EQ(tokens[0].value, "false");
    }

    PASS();
}

// ============================================================================
// Test: A keyword (a = rdf:type)
// ============================================================================
void testAKeyword() {
    TEST("SPARQL lexer 'a' keyword (rdf:type)");

    SparqlLexer lexer;

    // In SPARQL, standalone 'a' is rdf:type
    {
        auto tokens = lexer.tokenize("a");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, A);
    }

    PASS();
}

// ============================================================================
// Test: Error on invalid input
// ============================================================================
void testErrorOnInvalid() {
    TEST("SPARQL lexer error on invalid input");

    SparqlLexer lexer;

    // Truly invalid characters (e.g. standalone '@' or '^') produce Error tokens.
    // Note: unterminated IRIs/strings are handled by producing IRI/LITERAL tokens
    // with the text read so far, rather than Error tokens.
    {
        auto tokens = lexer.tokenize("@");
        ASSERT_TRUE(!tokens.empty());
        bool hasError = false;
        for (const auto& t : tokens) {
            if (t.type == Error) { hasError = true; break; }
        }
        ASSERT_TRUE(hasError);
    }

    // Unterminated IRI produces an IRI token (not Error) — just verify it doesn't crash
    {
        auto tokens = lexer.tokenize("<http://unterminated");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, IRI);
    }

    // Unterminated string produces a LITERAL token (not Error) — just verify it doesn't crash
    {
        auto tokens = lexer.tokenize("\"unterminated");
        ASSERT_TRUE(!tokens.empty());
        ASSERT_EQ(tokens[0].type, LITERAL);
    }

    PASS();
}

// ============================================================================
// Test: EOF token at end
// ============================================================================
void testEOFToken() {
    TEST("SPARQL lexer EOF token");

    SparqlLexer lexer;

    {
        auto tokens = lexer.tokenize("SELECT ?x");
        ASSERT_TRUE(tokens.size() >= 3);
        // Last token should be EOF
        ASSERT_EQ(tokens.back().type, EOF_);
    }

    {
        auto tokens = lexer.tokenize("");
        ASSERT_TRUE(tokens.size() >= 1);
        ASSERT_EQ(tokens[0].type, EOF_);
    }

    PASS();
}

// ============================================================================
// Test: Complex query tokenization
// ============================================================================
void testComplexQuery() {
    TEST("SPARQL lexer complex query");

    SparqlLexer lexer;

    auto tokens = lexer.tokenize(
        "SELECT ?name WHERE { ?x <http://example.org/name> ?name . "
        "FILTER(?age > 30) } LIMIT 10"
    );

    ASSERT_TRUE(tokens.size() >= 12);

    // Check first few tokens
    ASSERT_EQ(tokens[0].type, SELECT);
    ASSERT_EQ(tokens[1].type, VARIABLE);
    ASSERT_EQ(tokens[1].value, "name");
    ASSERT_EQ(tokens[2].type, WHERE);
    ASSERT_EQ(tokens[3].type, LBRACE);
    ASSERT_EQ(tokens[4].type, VARIABLE);
    ASSERT_EQ(tokens[4].value, "x");
    ASSERT_EQ(tokens[5].type, IRI);

    // Find FILTER, LIMIT
    bool foundFilter = false, foundLimit = false;
    for (const auto& t : tokens) {
        if (t.type == FILTER) foundFilter = true;
        if (t.type == LIMIT) foundLimit = true;
    }
    ASSERT_TRUE(foundFilter);
    ASSERT_TRUE(foundLimit);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SPARQL Lexer Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testKeywords();
    testVariables();
    testIRIs();
    testStringLiterals();
    testNumbers();
    testPunctuation();
    testOperators();
    testBooleans();
    testAKeyword();
    testErrorOnInvalid();
    testEOFToken();
    testComplexQuery();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
