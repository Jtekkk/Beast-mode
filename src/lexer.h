#ifndef beast_lexer_h
#define beast_lexer_h

#include "common.h"

typedef enum {
    // Punctuation.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_DOTDOT, TOKEN_COLON,
    TOKEN_MINUS, TOKEN_PLUS, TOKEN_SLASH, TOKEN_STAR, TOKEN_PERCENT,
    TOKEN_EQUAL, TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL,
    TOKEN_STAR_EQUAL, TOKEN_SLASH_EQUAL,
    TOKEN_EQUAL_EQUAL, TOKEN_BANG_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    // Keywords.
    TOKEN_AND, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FN, TOKEN_FOR, TOKEN_IF, TOKEN_IN, TOKEN_LET, TOKEN_NIL,
    TOKEN_NOT, TOKEN_OR, TOKEN_RETURN, TOKEN_TRUE, TOKEN_WHILE,
    // Reserved for future versions; using one is a compile error.
    TOKEN_RESERVED,
    // Statement terminator: `;` or an eligible newline.
    TOKEN_TERM,
    TOKEN_ERROR, TOKEN_EOF,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
    int column;
} Token;

void initLexer(const char* source);
Token scanToken(void);
// Returns a pointer to the start of `line` (1-based) in the current source
// and its length, for diagnostics. Returns NULL if out of range.
const char* lexerLineStart(int line, int* length);

#endif
