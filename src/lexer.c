#include "lexer.h"

#include <string.h>

typedef struct {
    const char* source;
    const char* start;
    const char* current;
    const char* lineStart;
    int line;
    int groupingDepth; // unclosed ( and [ - newlines inside never terminate
    TokenType prevType;
} Lexer;

Lexer lexer;

void initLexer(const char* source) {
    lexer.source = source;
    lexer.start = source;
    lexer.current = source;
    lexer.lineStart = source;
    lexer.line = 1;
    lexer.groupingDepth = 0;
    lexer.prevType = TOKEN_TERM; // start of file: no terminator needed
}

static bool isAtEnd(void) { return *lexer.current == '\0'; }

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static char advance(void) { return *lexer.current++; }

static char peek(void) { return *lexer.current; }

static char peekNext(void) {
    if (isAtEnd()) return '\0';
    return lexer.current[1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer.start;
    token.length = (int)(lexer.current - lexer.start);
    token.line = lexer.line;
    token.column = (int)(lexer.start - lexer.lineStart) + 1;
    lexer.prevType = type;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lexer.line;
    token.column = (int)(lexer.start - lexer.lineStart) + 1;
    lexer.prevType = TOKEN_ERROR;
    return token;
}

// A newline ends the statement only after a token that can end one.
static bool newlineTerminates(void) {
    if (lexer.groupingDepth > 0) return false;
    switch (lexer.prevType) {
        case TOKEN_IDENTIFIER:
        case TOKEN_NUMBER:
        case TOKEN_STRING:
        case TOKEN_RIGHT_PAREN:
        case TOKEN_RIGHT_BRACKET:
        case TOKEN_RIGHT_BRACE:
        case TOKEN_TRUE:
        case TOKEN_FALSE:
        case TOKEN_NIL:
        case TOKEN_RETURN:
        case TOKEN_BREAK:
        case TOKEN_CONTINUE:
            return true;
        default:
            return false;
    }
}

// Skips insignificant whitespace and comments. When a statement-ending
// newline is found, fills *term with a TERM token (positioned at the
// newline, before it is consumed) and returns true.
static bool skipWhitespace(Token* term) {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '#':
                while (peek() != '\n' && !isAtEnd()) advance();
                break;
            case '\n':
                if (newlineTerminates()) {
                    lexer.start = lexer.current;
                    *term = makeToken(TOKEN_TERM);
                    term->length = 0;
                    advance();
                    lexer.line++;
                    lexer.lineStart = lexer.current;
                    return true;
                }
                advance();
                lexer.line++;
                lexer.lineStart = lexer.current;
                break;
            default:
                return false;
        }
    }
}

static TokenType checkKeyword(int start, int length, const char* rest,
                              TokenType type) {
    if (lexer.current - lexer.start == start + length &&
        memcmp(lexer.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(void) {
    switch (lexer.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'o':
                        if (checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE) ==
                            TOKEN_CONTINUE) {
                            return TOKEN_CONTINUE;
                        }
                        return checkKeyword(2, 3, "nst", TOKEN_RESERVED);
                    case 'l': return checkKeyword(2, 3, "ass", TOKEN_RESERVED);
                }
            }
            break;
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'n': return checkKeyword(2, 0, "", TOKEN_FN);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'f': return checkKeyword(2, 0, "", TOKEN_IF);
                    case 'n': return checkKeyword(2, 0, "", TOKEN_IN);
                    case 'm': return checkKeyword(2, 4, "port", TOKEN_RESERVED);
                }
            }
            break;
        case 'l': return checkKeyword(1, 2, "et", TOKEN_LET);
        case 'm': return checkKeyword(1, 4, "atch", TOKEN_RESERVED);
        case 'n':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'i': return checkKeyword(2, 1, "l", TOKEN_NIL);
                    case 'o': return checkKeyword(2, 1, "t", TOKEN_NOT);
                }
            }
            break;
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 't':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'r':
                        if (checkKeyword(2, 2, "ue", TOKEN_TRUE) ==
                            TOKEN_TRUE) {
                            return TOKEN_TRUE;
                        }
                        return checkKeyword(2, 1, "y", TOKEN_RESERVED);
                }
            }
            break;
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token number(void) {
    while (isDigit(peek())) advance();
    // Only consume '.' for a fraction, never the '..' range operator.
    if (peek() == '.' && isDigit(peekNext())) {
        advance();
        while (isDigit(peek())) advance();
    }
    if ((peek() == 'e' || peek() == 'E') &&
        (isDigit(peekNext()) ||
         ((peekNext() == '+' || peekNext() == '-') &&
          isDigit(lexer.current[2])))) {
        advance(); // e
        if (peek() == '+' || peek() == '-') advance();
        while (isDigit(peek())) advance();
    }
    return makeToken(TOKEN_NUMBER);
}

static Token string(void) {
    while (peek() != '"' && peek() != '\n' && !isAtEnd()) {
        if (peek() == '\\' && peekNext() != '\0' && peekNext() != '\n') {
            advance(); // consume '\' plus the escaped character
        }
        advance();
    }
    if (peek() != '"') return errorToken("Unterminated string.");
    advance();
    return makeToken(TOKEN_STRING);
}

Token scanToken(void) {
    Token term;
    if (skipWhitespace(&term)) return term;
    lexer.start = lexer.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(': lexer.groupingDepth++; return makeToken(TOKEN_LEFT_PAREN);
        case ')':
            if (lexer.groupingDepth > 0) lexer.groupingDepth--;
            return makeToken(TOKEN_RIGHT_PAREN);
        case '[': lexer.groupingDepth++; return makeToken(TOKEN_LEFT_BRACKET);
        case ']':
            if (lexer.groupingDepth > 0) lexer.groupingDepth--;
            return makeToken(TOKEN_RIGHT_BRACKET);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ',': return makeToken(TOKEN_COMMA);
        case ':': return makeToken(TOKEN_COLON);
        case ';': return makeToken(TOKEN_TERM);
        case '.':
            if (match('.')) return makeToken(TOKEN_DOTDOT);
            return makeToken(TOKEN_DOT);
        case '+':
            return makeToken(match('=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
        case '-':
            return makeToken(match('=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
        case '*':
            return makeToken(match('=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
        case '/':
            return makeToken(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
        case '%': return makeToken(TOKEN_PERCENT);
        case '=':
            return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '!':
            if (match('=')) return makeToken(TOKEN_BANG_EQUAL);
            return errorToken("Unexpected '!'; Beast spells negation 'not'.");
        case '<':
            return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string();
    }

    return errorToken("Unexpected character.");
}

const char* lexerLineStart(int line, int* length) {
    const char* p = lexer.source;
    int current = 1;
    while (current < line && *p != '\0') {
        if (*p == '\n') current++;
        p++;
    }
    if (current != line) return NULL;
    const char* end = p;
    while (*end != '\0' && *end != '\n') end++;
    *length = (int)(end - p);
    return p;
}
