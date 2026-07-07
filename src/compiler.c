#include "compiler.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "memory.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    bool compiledAssignment;
    const char* path;
    bool replMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // = += -= *= /=   (statement level only)
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_NOT,        // not
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * / %
    PREC_UNARY,      // unary -
    PREC_CALL,       // . () []
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth; // -1 = declared but not yet initialized
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;

    // Peephole state: offsets/opcodes of the last two emitted instructions,
    // and a fence below which rewriting is forbidden (a jump target or loop
    // start lives there).
    int lastEmitOffset;
    uint8_t lastEmitOp;
    int prevEmitOffset;
    uint8_t prevEmitOp;
    int foldFence;
} Compiler;

#define MAX_LOOP_JUMPS 64

typedef struct Loop {
    struct Loop* enclosing;
    int start;            // while: condition offset (continue target)
    bool isFor;
    int scopeDepth;       // locals deeper than this are popped by break
    int continuePopDepth; // locals deeper than this are popped by continue
    int breakJumps[MAX_LOOP_JUMPS];
    int breakCount;
    int continueJumps[MAX_LOOP_JUMPS]; // for-loops: patched forward
    int continueCount;
} Loop;

// A global referenced during this compile; used for the end-of-compile
// undefined check and its did-you-mean hint.
typedef struct {
    int index;
    int line;
    int column;
} GlobalRef;

Parser parser;
Compiler* current = NULL;
static Loop* currentLoop = NULL;

static GlobalRef* globalRefs = NULL;
static int globalRefCount = 0;
static int globalRefCapacity = 0;
static bool* definedGlobals = NULL; // indexed by global slot, this compile
static int definedGlobalsCapacity = 0;

static Chunk* currentChunk(void) { return &current->function->chunk; }

// ---------------------------------------------------------------- errors

static void printDiagnostic(Token* token, const char* message,
                            const char* hint) {
    fprintf(stderr, "%s:%d:%d: error", parser.path, token->line,
            token->column);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_TERM) {
        fprintf(stderr, " at end of line");
    } else if (token->type != TOKEN_ERROR && token->length > 0) {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);

    int lineLength = 0;
    const char* lineStart = lexerLineStart(token->line, &lineLength);
    if (lineStart != NULL) {
        fprintf(stderr, " %4d | %.*s\n", token->line, lineLength, lineStart);
        fprintf(stderr, "      | ");
        int caret = token->column - 1;
        if (caret > lineLength) caret = lineLength;
        for (int i = 0; i < caret; i++) {
            fputc(lineStart[i] == '\t' ? '\t' : ' ', stderr);
        }
        fprintf(stderr, "^\n");
    }
    if (hint != NULL) fprintf(stderr, "  hint: %s\n", hint);
}

static void errorAtHint(Token* token, const char* message, const char* hint) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    printDiagnostic(token, message, hint);
    parser.hadError = true;
}

static void errorAt(Token* token, const char* message) {
    errorAtHint(token, message, NULL);
}

static void error(const char* message) { errorAt(&parser.previous, message); }

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// ---------------------------------------------------------------- parsing

static void advance(void) {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// ---------------------------------------------------------------- emitting

static void noteInstruction(uint8_t op) {
    current->prevEmitOffset = current->lastEmitOffset;
    current->prevEmitOp = current->lastEmitOp;
    current->lastEmitOffset = currentChunk()->count;
    current->lastEmitOp = op;
}

static void forgetInstructions(void) {
    current->lastEmitOffset = -1;
    current->lastEmitOp = 0xff;
    current->prevEmitOffset = -1;
    current->prevEmitOp = 0xff;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitOp(uint8_t op) {
    noteInstruction(op);
    emitByte(op);
}

static void emitOpByte(uint8_t op, uint8_t operand) {
    emitOp(op);
    emitByte(operand);
}

static void emitOpShort(uint8_t op, uint16_t operand) {
    emitOp(op);
    emitByte((operand >> 8) & 0xff);
    emitByte(operand & 0xff);
}

static void setFence(void) { current->foldFence = currentChunk()->count; }

static int emitJump(uint8_t instruction) {
    // Fuse compare + OP_JUMP_IF_FALSE into one branch opcode when the
    // comparison is the immediately preceding instruction and no jump
    // target sits between them.
    if (instruction == OP_JUMP_IF_FALSE &&
        current->lastEmitOffset >= current->foldFence &&
        current->lastEmitOffset == currentChunk()->count - 1) {
        uint8_t fused = 0xff;
        switch (current->lastEmitOp) {
            case OP_LESS: fused = OP_JUMP_IF_NOT_LESS; break;
            case OP_LESS_EQUAL: fused = OP_JUMP_IF_NOT_LESS_EQUAL; break;
            case OP_GREATER: fused = OP_JUMP_IF_NOT_GREATER; break;
            case OP_GREATER_EQUAL:
                fused = OP_JUMP_IF_NOT_GREATER_EQUAL;
                break;
            case OP_EQUAL: fused = OP_JUMP_IF_NOT_EQUAL; break;
            case OP_NOT_EQUAL: fused = OP_JUMP_IF_EQUAL; break;
            default: break;
        }
        if (fused != 0xff) {
            currentChunk()->code[current->lastEmitOffset] = fused;
            current->lastEmitOp = fused;
            emitByte(0xff);
            emitByte(0xff);
            return currentChunk()->count - 2;
        }
    }
    emitOp(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    // -2 to adjust for the operand itself.
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
    setFence();
}

static void emitLoop(int loopStart) {
    emitOp(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static void emitReturn(void) {
    emitOp(OP_NIL);
    emitOp(OP_RETURN);
}

static uint16_t makeConstant(Value value) {
    if (IS_OBJ(value)) pushTempRoot(value);
    int constant = addConstant(currentChunk(), value);
    if (IS_OBJ(value)) popTempRoot();
    if (constant > UINT16_MAX) {
        error("Too many constants in one function.");
        return 0;
    }
    return (uint16_t)constant;
}

static void emitConstant(Value value) {
    emitOpShort(OP_CONSTANT, makeConstant(value));
}

// ------------------------------------------------------------- optimizer

// Truncate the chunk to `offset`, dropping any line entries past it.
static void truncateChunk(int offset) {
    Chunk* chunk = currentChunk();
    chunk->count = offset;
    while (chunk->lineCount > 0 &&
           chunk->lines[chunk->lineCount - 1].offset >= offset) {
        chunk->lineCount--;
    }
}

static Value readConstantAt(int instrOffset) {
    Chunk* chunk = currentChunk();
    uint16_t index = (uint16_t)((chunk->code[instrOffset + 1] << 8) |
                                chunk->code[instrOffset + 2]);
    return chunk->constants.values[index];
}

// Fold `<num literal> op <num literal>` at emit time. The two OP_CONSTANTs
// must be the last two instructions with no jump target between them.
static bool tryFoldBinary(TokenType op) {
    Compiler* c = current;
    if (c->lastEmitOp != OP_CONSTANT || c->prevEmitOp != OP_CONSTANT) {
        return false;
    }
    if (c->prevEmitOffset < c->foldFence) return false;
    if (c->lastEmitOffset != c->prevEmitOffset + 3) return false;
    if (currentChunk()->count != c->lastEmitOffset + 3) return false;

    Value a = readConstantAt(c->prevEmitOffset);
    Value b = readConstantAt(c->lastEmitOffset);
    if (!IS_NUM(a) || !IS_NUM(b)) return false;

    double x = AS_NUM(a);
    double y = AS_NUM(b);
    double result;
    switch (op) {
        case TOKEN_PLUS: result = x + y; break;
        case TOKEN_MINUS: result = x - y; break;
        case TOKEN_STAR: result = x * y; break;
        case TOKEN_SLASH: result = x / y; break;
        case TOKEN_PERCENT: result = fmod(x, y); break;
        default: return false;
    }

    truncateChunk(c->prevEmitOffset);
    forgetInstructions();
    emitConstant(NUM_VAL(result));
    return true;
}

// Fold unary minus on a number literal by negating the constant in place.
static bool tryFoldNegate(void) {
    Compiler* c = current;
    if (c->lastEmitOp != OP_CONSTANT) return false;
    if (c->lastEmitOffset < c->foldFence) return false;
    if (currentChunk()->count != c->lastEmitOffset + 3) return false;

    Chunk* chunk = currentChunk();
    uint16_t index = (uint16_t)((chunk->code[c->lastEmitOffset + 1] << 8) |
                                chunk->code[c->lastEmitOffset + 2]);
    Value v = chunk->constants.values[index];
    if (!IS_NUM(v)) return false;
    chunk->constants.values[index] = NUM_VAL(-AS_NUM(v));
    return true;
}

// ----------------------------------------------------------- scope/locals

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->lastEmitOffset = -1;
    compiler->lastEmitOp = 0xff;
    compiler->prevEmitOffset = -1;
    compiler->prevEmitOp = 0xff;
    compiler->foldFence = 0;
    compiler->function = newFunction();
    current = compiler;
    if (type != TYPE_SCRIPT) {
        if (parser.previous.type == TOKEN_IDENTIFIER) {
            current->function->name =
                copyString(parser.previous.start, parser.previous.length);
        } else {
            current->function->name = copyString("(anon)", 6);
        }
    }

    // Slot zero holds the called closure itself.
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    local->name.length = 0;
}

static ObjFunction* endCompiler(void) {
    emitReturn();
    ObjFunction* function = current->function;
    current = current->enclosing;
    return function;
}

static void beginScope(void) { current->scopeDepth++; }

// Emits pops for locals deeper than targetDepth without touching compiler
// bookkeeping. Used by break/continue, which jump over the rest of the
// scope: a local may become captured *after* this point in the body, so
// every slot is discarded with OP_CLOSE_UPVALUE (a safe superset of OP_POP).
static void emitPopsToDepth(int targetDepth) {
    for (int i = current->localCount - 1;
         i >= 0 && current->locals[i].depth > targetDepth; i--) {
        emitOp(OP_CLOSE_UPVALUE);
    }
}

static void endScope(void) {
    current->scopeDepth--;
    int pending = 0;
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth >
               current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            while (pending > 255) { emitOpByte(OP_POPN, 255); pending -= 255; }
            if (pending > 1) { emitOpByte(OP_POPN, (uint8_t)pending); pending = 0; }
            else if (pending == 1) { emitOp(OP_POP); pending = 0; }
            emitOp(OP_CLOSE_UPVALUE);
        } else {
            pending++;
        }
        current->localCount--;
    }
    while (pending > 255) { emitOpByte(OP_POPN, 255); pending -= 255; }
    if (pending > 1) { emitOpByte(OP_POPN, (uint8_t)pending); }
    else if (pending == 1) { emitOp(OP_POP); }
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read a local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }
    if (upvalueCount == UINT8_COUNT) {
        error("Too many captured variables in one function.");
        return 0;
    }
    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }
    return -1;
}

static int addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in one function.");
        return -1;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
    return current->localCount - 1;
}

static Token syntheticToken(const char* text) {
    Token token;
    token.type = TOKEN_IDENTIFIER;
    token.start = text;
    token.length = (int)strlen(text);
    token.line = parser.previous.line;
    token.column = parser.previous.column;
    return token;
}

// Adds an already-initialized, unnamed local for a value the compiler keeps
// on the stack (for-loop counter/limit). Returns its slot.
static int addHiddenLocal(const char* debugName) {
    Token token = syntheticToken(debugName);
    int slot = addLocal(token);
    if (slot >= 0) current->locals[slot].depth = current->scopeDepth;
    return slot;
}

static void declareLocal(void) {
    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) {
            error("A variable with this name is already declared in this scope.");
        }
    }
    addLocal(*name);
}

static void markInitialized(void) {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// ---------------------------------------------------------------- globals

static void ensureDefinedCapacity(int index) {
    if (index < definedGlobalsCapacity) return;
    int newCapacity = definedGlobalsCapacity < 64 ? 64 : definedGlobalsCapacity;
    while (newCapacity <= index) newCapacity *= 2;
    definedGlobals = (bool*)realloc(definedGlobals, newCapacity);
    if (definedGlobals == NULL) exit(74);
    memset(definedGlobals + definedGlobalsCapacity, 0,
           newCapacity - definedGlobalsCapacity);
    definedGlobalsCapacity = newCapacity;
}

static uint16_t globalIndexFor(Token* name) {
    ObjString* string = copyString(name->start, name->length);
    int index = vmGlobalIndex(string);
    if (index > UINT16_MAX) {
        error("Too many global variables.");
        return 0;
    }
    return (uint16_t)index;
}

static void recordGlobalRef(int index, Token* name) {
    for (int i = 0; i < globalRefCount; i++) {
        if (globalRefs[i].index == index) return;
    }
    if (globalRefCount + 1 > globalRefCapacity) {
        globalRefCapacity = globalRefCapacity < 16 ? 16 : globalRefCapacity * 2;
        globalRefs =
            (GlobalRef*)realloc(globalRefs, sizeof(GlobalRef) * globalRefCapacity);
        if (globalRefs == NULL) exit(74);
    }
    globalRefs[globalRefCount].index = index;
    globalRefs[globalRefCount].line = name->line;
    globalRefs[globalRefCount].column = name->column;
    globalRefCount++;
}

static void markGlobalDefined(int index) {
    ensureDefinedCapacity(index);
    definedGlobals[index] = true;
}

static bool isGlobalDefined(int index) {
    if (index < definedGlobalsCapacity && definedGlobals[index]) return true;
    return !IS_UNDEFINED(vm.globals.values[index]);
}

// Levenshtein distance with early exit; used for did-you-mean hints.
static int editDistance(const char* a, int alen, const char* b, int blen) {
    if (alen > 32 || blen > 32) return 99;
    int row[33];
    for (int j = 0; j <= blen; j++) row[j] = j;
    for (int i = 1; i <= alen; i++) {
        int prev = row[0];
        row[0] = i;
        for (int j = 1; j <= blen; j++) {
            int cur = row[j];
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int best = prev + cost;
            if (row[j] + 1 < best) best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = best;
            prev = cur;
        }
    }
    return row[blen];
}

static void checkUndefinedGlobals(void) {
    for (int i = 0; i < globalRefCount; i++) {
        int index = globalRefs[i].index;
        if (isGlobalDefined(index)) continue;

        ObjString* name = AS_STRING(vm.globalNames.values[index]);
        char message[160];
        snprintf(message, sizeof(message), "Undefined global '%s'.",
                 name->chars);

        // Find the closest defined global for a hint.
        const char* best = NULL;
        int bestDistance = 3;
        for (int j = 0; j < vm.globalNames.count; j++) {
            if (j == index || !isGlobalDefined(j)) continue;
            ObjString* candidate = AS_STRING(vm.globalNames.values[j]);
            int distance = editDistance(name->chars, name->length,
                                        candidate->chars, candidate->length);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate->chars;
            }
        }
        char hint[128];
        if (best != NULL) {
            snprintf(hint, sizeof(hint), "did you mean '%s'?", best);
        }

        Token token;
        token.type = TOKEN_IDENTIFIER;
        token.start = name->chars;
        token.length = name->length;
        token.line = globalRefs[i].line;
        token.column = globalRefs[i].column;
        parser.panicMode = false;
        errorAtHint(&token, message, best != NULL ? hint : NULL);
    }
}

// ------------------------------------------------------------ expressions

static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUM_VAL(value));
}

static void string(bool canAssign) {
    (void)canAssign;
    const char* src = parser.previous.start + 1; // skip opening quote
    int srcLength = parser.previous.length - 2;  // and closing quote
    char* buffer = (char*)malloc(srcLength + 1);
    if (buffer == NULL) exit(74);
    int length = 0;
    for (int i = 0; i < srcLength; i++) {
        char c = src[i];
        if (c == '\\' && i + 1 < srcLength) {
            i++;
            switch (src[i]) {
                case 'n': buffer[length++] = '\n'; break;
                case 't': buffer[length++] = '\t'; break;
                case 'r': buffer[length++] = '\r'; break;
                case '"': buffer[length++] = '"'; break;
                case '\\': buffer[length++] = '\\'; break;
                default: {
                    char message[64];
                    snprintf(message, sizeof(message),
                             "Unknown escape sequence '\\%c'.", src[i]);
                    error(message);
                    free(buffer);
                    return;
                }
            }
        } else {
            buffer[length++] = c;
        }
    }
    ObjString* value = copyString(buffer, length);
    free(buffer);
    emitConstant(OBJ_VAL(value));
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitOp(OP_FALSE); break;
        case TOKEN_TRUE: emitOp(OP_TRUE); break;
        case TOKEN_NIL: emitOp(OP_NIL); break;
        default: return;
    }
}

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);
    if (operatorType == TOKEN_MINUS) {
        if (!tryFoldNegate()) emitOp(OP_NEGATE);
    }
}

static void not_(bool canAssign) {
    (void)canAssign;
    parsePrecedence(PREC_NOT);
    emitOp(OP_NOT);
}

static void binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
            if (tryFoldBinary(operatorType)) return;
            break;
        default:
            break;
    }

    switch (operatorType) {
        case TOKEN_PLUS: emitOp(OP_ADD); break;
        case TOKEN_MINUS: emitOp(OP_SUB); break;
        case TOKEN_STAR: emitOp(OP_MUL); break;
        case TOKEN_SLASH: emitOp(OP_DIV); break;
        case TOKEN_PERCENT: emitOp(OP_MOD); break;
        case TOKEN_EQUAL_EQUAL: emitOp(OP_EQUAL); break;
        case TOKEN_BANG_EQUAL: emitOp(OP_NOT_EQUAL); break;
        case TOKEN_LESS: emitOp(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitOp(OP_LESS_EQUAL); break;
        case TOKEN_GREATER: emitOp(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitOp(OP_GREATER_EQUAL); break;
        default: return;
    }
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE_PEEK);
    emitOp(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_TRUE_PEEK);
    emitOp(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

// Returns the matched assignment operator, or TOKEN_ERROR if none.
static TokenType matchAssignment(void) {
    if (match(TOKEN_EQUAL)) return TOKEN_EQUAL;
    if (match(TOKEN_PLUS_EQUAL)) return TOKEN_PLUS_EQUAL;
    if (match(TOKEN_MINUS_EQUAL)) return TOKEN_MINUS_EQUAL;
    if (match(TOKEN_STAR_EQUAL)) return TOKEN_STAR_EQUAL;
    if (match(TOKEN_SLASH_EQUAL)) return TOKEN_SLASH_EQUAL;
    return TOKEN_ERROR;
}

static uint8_t compoundOp(TokenType type) {
    switch (type) {
        case TOKEN_PLUS_EQUAL: return OP_ADD;
        case TOKEN_MINUS_EQUAL: return OP_SUB;
        case TOKEN_STAR_EQUAL: return OP_MUL;
        default: return OP_DIV;
    }
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    bool isGlobal = false;
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = globalIndexFor(&name);
        recordGlobalRef(arg, &name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
        isGlobal = true;
    }

    TokenType assignment = canAssign ? matchAssignment() : TOKEN_ERROR;
    if (assignment == TOKEN_EQUAL) {
        expression();
    } else if (assignment != TOKEN_ERROR) {
        if (isGlobal) {
            emitOpShort(getOp, (uint16_t)arg);
        } else {
            emitOpByte(getOp, (uint8_t)arg);
        }
        expression();
        emitOp(compoundOp(assignment));
    } else {
        if (isGlobal) {
            emitOpShort(getOp, (uint16_t)arg);
        } else {
            emitOpByte(getOp, (uint8_t)arg);
        }
        return;
    }
    if (isGlobal) {
        emitOpShort(setOp, (uint16_t)arg);
    } else {
        emitOpByte(setOp, (uint8_t)arg);
    }
    parser.compiledAssignment = true;
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect a field name after '.'.");
    ObjString* name =
        copyString(parser.previous.start, parser.previous.length);
    uint16_t constant = makeConstant(OBJ_VAL(name));

    TokenType assignment = canAssign ? matchAssignment() : TOKEN_ERROR;
    if (assignment == TOKEN_EQUAL) {
        emitOpShort(OP_CONSTANT, constant);
        expression();
        emitOp(OP_INDEX_SET);
        parser.compiledAssignment = true;
    } else if (assignment != TOKEN_ERROR) {
        emitOpShort(OP_CONSTANT, constant);
        emitOp(OP_DUP2);
        emitOp(OP_INDEX_GET);
        expression();
        emitOp(compoundOp(assignment));
        emitOp(OP_INDEX_SET);
        parser.compiledAssignment = true;
    } else {
        emitOpShort(OP_CONSTANT, constant);
        emitOp(OP_INDEX_GET);
    }
}

static void index_(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    TokenType assignment = canAssign ? matchAssignment() : TOKEN_ERROR;
    if (assignment == TOKEN_EQUAL) {
        expression();
        emitOp(OP_INDEX_SET);
        parser.compiledAssignment = true;
    } else if (assignment != TOKEN_ERROR) {
        emitOp(OP_DUP2);
        emitOp(OP_INDEX_GET);
        expression();
        emitOp(compoundOp(assignment));
        emitOp(OP_INDEX_SET);
        parser.compiledAssignment = true;
    } else {
        emitOp(OP_INDEX_GET);
    }
}

static uint8_t argumentList(void) {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (check(TOKEN_RIGHT_PAREN)) break; // allow trailing comma
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void call(bool canAssign) {
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitOpByte(OP_CALL, argCount);
}

static void arrayLiteral(bool canAssign) {
    (void)canAssign;
    int count = 0;
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            if (check(TOKEN_RIGHT_BRACKET)) break; // allow trailing comma
            expression();
            count++;
            if (count > UINT16_MAX) {
                error("Too many elements in one array literal.");
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
    emitOpShort(OP_ARRAY, (uint16_t)count);
}

static void mapLiteral(bool canAssign) {
    (void)canAssign;
    int count = 0;
    while (match(TOKEN_TERM)) {} // newlines are free inside map literals
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            while (match(TOKEN_TERM)) {}
            if (check(TOKEN_RIGHT_BRACE)) break; // allow trailing comma
            if (match(TOKEN_IDENTIFIER)) {
                // Bare identifier keys are string sugar: { rex: 7 }.
                ObjString* key = copyString(parser.previous.start,
                                            parser.previous.length);
                emitConstant(OBJ_VAL(key));
            } else if (match(TOKEN_STRING)) {
                string(false);
            } else if (match(TOKEN_NUMBER)) {
                number(false);
            } else {
                errorAtCurrent(
                    "Expect a map key (identifier, string, or number).");
                break;
            }
            consume(TOKEN_COLON, "Expect ':' after map key.");
            expression();
            count++;
            if (count > UINT16_MAX) {
                error("Too many entries in one map literal.");
            }
            while (match(TOKEN_TERM)) {}
        } while (match(TOKEN_COMMA));
    }
    while (match(TOKEN_TERM)) {}
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after map entries.");
    emitOpShort(OP_MAP, (uint16_t)count);
}

static void function(FunctionType type);

static void fnExpression(bool canAssign) {
    (void)canAssign;
    function(TYPE_FUNCTION);
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {mapLiteral, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {arrayLiteral, index_, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_DOTDOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT] = {NULL, binary, PREC_FACTOR},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_PLUS_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_STAR_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_BREAK] = {NULL, NULL, PREC_NONE},
    [TOKEN_CONTINUE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FN] = {fnExpression, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_IN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_NOT] = {not_, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RESERVED] = {NULL, NULL, PREC_NONE},
    [TOKEN_TERM] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        if (parser.previous.type == TOKEN_RESERVED) {
            char message[96];
            snprintf(message, sizeof(message),
                     "'%.*s' is reserved for a future version of Beast.",
                     parser.previous.length, parser.previous.start);
            error(message);
        } else {
            error("Expect an expression.");
        }
        return;
    }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    // Assignment is a statement: once a handler compiled one, stop parsing.
    // Only the statement-level call (canAssign) may observe the flag.
    if (canAssign && parser.compiledAssignment) return;

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
        if (canAssign && parser.compiledAssignment) return;
    }

    if (check(TOKEN_EQUAL) || check(TOKEN_PLUS_EQUAL) ||
        check(TOKEN_MINUS_EQUAL) || check(TOKEN_STAR_EQUAL) ||
        check(TOKEN_SLASH_EQUAL)) {
        if (canAssign) {
            errorAtCurrent("Invalid assignment target.");
        } else {
            errorAtHint(&parser.current,
                        "Assignment is a statement, not an expression.",
                        parser.current.type == TOKEN_EQUAL
                            ? "did you mean '=='?"
                            : NULL);
        }
    }
}

static void expression(void) { parsePrecedence(PREC_OR); }

// ------------------------------------------------------------- statements

static void block(void) {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (match(TOKEN_TERM)) continue;
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (check(TOKEN_RIGHT_PAREN)) break; // allow trailing comma
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            consume(TOKEN_IDENTIFIER, "Expect a parameter name.");
            declareLocal();
            markInitialized();
            if (check(TOKEN_COLON)) {
                errorAtCurrent(
                    "Type annotations are reserved for a future version of "
                    "Beast.");
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    pushTempRoot(OBJ_VAL(function));
    uint16_t constant = makeConstant(OBJ_VAL(function));
    popTempRoot();
    emitOpShort(OP_CLOSURE, constant);
    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void consumeTerminator(void) {
    if (match(TOKEN_TERM)) return;
    if (check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)) return;
    if (check(TOKEN_DOTDOT)) {
        errorAtCurrent("'..' ranges are only valid in a 'for' header.");
        return;
    }
    errorAtCurrent("Expect a newline or ';' after this statement.");
}

static void fnDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "Expect a function name after 'fn'.");
    Token name = parser.previous;
    if (current->scopeDepth > 0) {
        declareLocal();
        markInitialized(); // allow recursion through the local slot
        function(TYPE_FUNCTION);
    } else {
        uint16_t global = globalIndexFor(&name);
        if (!parser.replMode && current->type == TYPE_SCRIPT &&
            global < definedGlobalsCapacity && definedGlobals[global]) {
            error("Global with this name is already defined.");
        }
        markGlobalDefined(global);
        function(TYPE_FUNCTION);
        emitOpShort(OP_DEFINE_GLOBAL, global);
    }
}

static void letDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "Expect a variable name after 'let'.");
    Token name = parser.previous;
    if (current->scopeDepth > 0) {
        declareLocal();
        consume(TOKEN_EQUAL, "Expect '=' after the variable name; 'let' "
                             "requires an initializer.");
        expression();
        markInitialized();
    } else {
        uint16_t global = globalIndexFor(&name);
        if (!parser.replMode && global < definedGlobalsCapacity &&
            definedGlobals[global]) {
            error("Global with this name is already defined.");
        }
        consume(TOKEN_EQUAL, "Expect '=' after the variable name; 'let' "
                             "requires an initializer.");
        expression();
        markGlobalDefined(global);
        emitOpShort(OP_DEFINE_GLOBAL, global);
    }
    consumeTerminator();
}

static void expressionStatement(void) {
    parser.compiledAssignment = false;
    parsePrecedence(PREC_ASSIGNMENT);
    if (!parser.compiledAssignment) {
        if (parser.replMode && current->type == TYPE_SCRIPT &&
            current->scopeDepth == 0) {
            emitOp(OP_ECHO);
        } else {
            emitOp(OP_POP);
        }
    }
    consumeTerminator();
}

static void ifStatement(void) {
    expression();
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    consume(TOKEN_LEFT_BRACE, "Expect '{' after the if condition.");
    beginScope();
    block();
    endScope();

    if (check(TOKEN_ELSE)) {
        int elseJump = emitJump(OP_JUMP);
        patchJump(thenJump);
        advance(); // consume else
        if (match(TOKEN_IF)) {
            ifStatement();
        } else {
            consume(TOKEN_LEFT_BRACE, "Expect '{' or 'if' after 'else'.");
            beginScope();
            block();
            endScope();
        }
        patchJump(elseJump);
    } else {
        patchJump(thenJump);
    }
}

static void beginLoop(Loop* loop, bool isFor) {
    loop->enclosing = currentLoop;
    loop->isFor = isFor;
    loop->start = -1;
    loop->scopeDepth = current->scopeDepth;
    loop->continuePopDepth = current->scopeDepth;
    loop->breakCount = 0;
    loop->continueCount = 0;
    currentLoop = loop;
}

static void endLoop(void) {
    Loop* loop = currentLoop;
    for (int i = 0; i < loop->breakCount; i++) {
        patchJump(loop->breakJumps[i]);
    }
    currentLoop = loop->enclosing;
}

static void whileStatement(void) {
    Loop loop;
    beginLoop(&loop, false);
    loop.start = currentChunk()->count;
    setFence();

    expression();
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    consume(TOKEN_LEFT_BRACE, "Expect '{' after the while condition.");
    beginScope();
    block();
    endScope();
    emitLoop(loop.start);
    patchJump(exitJump);
    endLoop();
    setFence();
}

static void forStatement(void) {
    beginScope(); // hidden counter and limit live in this scope

    consume(TOKEN_IDENTIFIER, "Expect a loop variable name after 'for'.");
    Token name = parser.previous;
    consume(TOKEN_IN, "Expect 'in' after the loop variable.");
    expression(); // range start -> hidden counter slot
    int counterSlot = addHiddenLocal("(for-counter)");
    consume(TOKEN_DOTDOT, "Expect '..' between range bounds.");
    expression(); // range limit -> hidden limit slot
    addHiddenLocal("(for-limit)");

    if (counterSlot > UINT8_MAX) {
        error("Too many local variables in one function.");
        counterSlot = 0;
    }

    // OP_FOR_PREP validates the bounds; if counter < limit it pushes the
    // first iteration value, otherwise it jumps past the loop.
    emitOp(OP_FOR_PREP);
    emitByte((uint8_t)counterSlot);
    emitByte(0xff);
    emitByte(0xff);
    int prepJump = currentChunk()->count - 2;

    Loop loop;
    beginLoop(&loop, true);
    loop.continuePopDepth = current->scopeDepth + 1;

    beginScope(); // visible loop variable: fresh binding each iteration
    parser.previous = name; // declareLocal reads parser.previous
    declareLocal();
    markInitialized();

    int loopStart = currentChunk()->count;
    setFence();

    consume(TOKEN_LEFT_BRACE, "Expect '{' after the for header.");
    beginScope();
    block();
    endScope();

    // Continue lands here: discard this iteration's loop variable, then
    // OP_FOR_RANGE advances and re-enters the loop with a fresh one.
    for (int i = 0; i < loop.continueCount; i++) {
        patchJump(loop.continueJumps[i]);
    }
    bool captured = current->locals[current->localCount - 1].isCaptured;
    emitOp(captured ? OP_CLOSE_UPVALUE : OP_POP);
    current->localCount--; // drop the loop variable without re-popping
    current->scopeDepth--;

    emitOp(OP_FOR_RANGE);
    emitByte((uint8_t)counterSlot);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);

    patchJump(prepJump);
    endLoop();
    endScope(); // pops hidden counter and limit
    setFence();
}

static void breakStatement(void) {
    if (currentLoop == NULL) {
        error("'break' is only allowed inside a loop.");
        consumeTerminator();
        return;
    }
    emitPopsToDepth(currentLoop->scopeDepth);
    if (currentLoop->breakCount == MAX_LOOP_JUMPS) {
        error("Too many 'break's in one loop.");
    } else {
        currentLoop->breakJumps[currentLoop->breakCount++] =
            emitJump(OP_JUMP);
    }
    consumeTerminator();
}

static void continueStatement(void) {
    if (currentLoop == NULL) {
        error("'continue' is only allowed inside a loop.");
        consumeTerminator();
        return;
    }
    emitPopsToDepth(currentLoop->continuePopDepth);
    if (currentLoop->isFor) {
        if (currentLoop->continueCount == MAX_LOOP_JUMPS) {
            error("Too many 'continue's in one loop.");
        } else {
            currentLoop->continueJumps[currentLoop->continueCount++] =
                emitJump(OP_JUMP);
        }
    } else {
        emitLoop(currentLoop->start);
    }
    consumeTerminator();
}

static void returnStatement(void) {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }
    if (check(TOKEN_TERM) || check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)) {
        emitReturn();
    } else {
        expression();
        emitOp(OP_RETURN);
    }
    consumeTerminator();
}

static void statement(void) {
    if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else if (check(TOKEN_ELSE)) {
        errorAtHint(&parser.current, "Unexpected 'else'.",
                    "'else' must be on the same line as the '}' that closes "
                    "the if body.");
        advance();
    } else {
        expressionStatement();
    }
}

static void synchronize(void) {
    parser.panicMode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_TERM) return;
        switch (parser.current.type) {
            case TOKEN_FN:
            case TOKEN_LET:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
            case TOKEN_RIGHT_BRACE:
                return;
            default:
                break;
        }
        advance();
    }
}

static void declaration(void) {
    if (check(TOKEN_RESERVED)) {
        char message[96];
        snprintf(message, sizeof(message),
                 "'%.*s' is reserved for a future version of Beast.",
                 parser.current.length, parser.current.start);
        errorAtCurrent(message);
        advance();
    } else if (match(TOKEN_FN)) {
        fnDeclaration();
    } else if (match(TOKEN_LET)) {
        letDeclaration();
    } else {
        statement();
    }
    if (parser.panicMode) synchronize();
}

ObjFunction* compile(const char* source, const char* path, bool replMode) {
    initLexer(source);
    parser.hadError = false;
    parser.panicMode = false;
    parser.compiledAssignment = false;
    parser.path = path;
    parser.replMode = replMode;
    globalRefCount = 0;

    Compiler compiler;
    current = NULL;
    initCompiler(&compiler, TYPE_SCRIPT);

    advance();
    while (!match(TOKEN_EOF)) {
        if (match(TOKEN_TERM)) continue;
        declaration();
    }

    ObjFunction* function = endCompiler();
    if (!replMode) checkUndefinedGlobals();

    if (definedGlobals != NULL) {
        memset(definedGlobals, 0, definedGlobalsCapacity);
    }
    return parser.hadError ? NULL : function;
}

void markCompilerRoots(void) {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
