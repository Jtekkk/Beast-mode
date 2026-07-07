#define _POSIX_C_SOURCE 200809L

#include "vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "compiler.h"
#include "debug.h"
#include "memory.h"

VM vm;

static void resetStack(void) {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
    vm.tempRootCount = 0;
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "runtime error: ");
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);

    // Deep traces (e.g. runaway recursion) are truncated in the middle so
    // the innermost and outermost frames both stay visible.
    const int headFrames = 10;
    const int tailFrames = 10;
    int n = vm.frameCount;
    for (int i = n - 1; i >= 0; i--) {
        int fromTop = n - 1 - i;
        if (n > headFrames + tailFrames + 1 && fromTop == headFrames) {
            fprintf(stderr, "  ... %d more frames ...\n",
                    n - headFrames - tailFrames);
            i = tailFrames; // skip to the outermost tailFrames frames
            continue;
        }
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        int line = getLine(&function->chunk, (int)instruction);
        fprintf(stderr, "  at %s (%s:%d)\n",
                function->name != NULL ? function->name->chars : "<script>",
                vm.scriptPath, line);
    }
    resetStack();
}

void push(Value value) { *vm.stackTop++ = value; }

Value pop(void) { return *--vm.stackTop; }

static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool isInt(double d) { return d == floor(d) && !isinf(d); }

int vmGlobalIndex(ObjString* name) {
    Value indexValue;
    if (tableGet(&vm.globalNameTable, OBJ_VAL(name), &indexValue)) {
        return (int)AS_NUM(indexValue);
    }
    pushTempRoot(OBJ_VAL(name));
    writeValueArray(&vm.globals, UNDEFINED_VAL);
    writeValueArray(&vm.globalNames, OBJ_VAL(name));
    int index = vm.globals.count - 1;
    tableSet(&vm.globalNameTable, OBJ_VAL(name), NUM_VAL(index));
    popTempRoot();
    return index;
}

// ------------------------------------------------------- string building

typedef struct {
    char* data;
    int length;
    int capacity;
} Buffer;

static void bufAppend(Buffer* buf, const char* chars, int length) {
    if (buf->length + length + 1 > buf->capacity) {
        int capacity = buf->capacity < 32 ? 32 : buf->capacity;
        while (capacity < buf->length + length + 1) capacity *= 2;
        buf->data = (char*)realloc(buf->data, capacity);
        if (buf->data == NULL) exit(74);
        buf->capacity = capacity;
    }
    memcpy(buf->data + buf->length, chars, length);
    buf->length += length;
}

#define MAX_STRINGIFY_DEPTH 8

static void bufValue(Buffer* buf, Value value, int depth, bool quote) {
    char scratch[64];
    if (IS_NUM(value)) {
        int n = formatNumber(scratch, sizeof(scratch), AS_NUM(value));
        bufAppend(buf, scratch, n);
    } else if (IS_NIL(value)) {
        bufAppend(buf, "nil", 3);
    } else if (IS_BOOL(value)) {
        if (AS_BOOL(value)) bufAppend(buf, "true", 4);
        else bufAppend(buf, "false", 5);
    } else if (IS_STRING(value)) {
        if (quote) bufAppend(buf, "\"", 1);
        bufAppend(buf, AS_CSTRING(value), AS_STRING(value)->length);
        if (quote) bufAppend(buf, "\"", 1);
    } else if (IS_ARRAY(value)) {
        if (depth >= MAX_STRINGIFY_DEPTH) {
            bufAppend(buf, "[...]", 5);
            return;
        }
        ObjArray* array = AS_ARRAY(value);
        bufAppend(buf, "[", 1);
        for (int i = 0; i < array->items.count; i++) {
            if (i > 0) bufAppend(buf, ", ", 2);
            bufValue(buf, array->items.values[i], depth + 1, true);
        }
        bufAppend(buf, "]", 1);
    } else if (IS_MAP(value)) {
        if (depth >= MAX_STRINGIFY_DEPTH) {
            bufAppend(buf, "{...}", 5);
            return;
        }
        ObjMap* map = AS_MAP(value);
        bufAppend(buf, "{", 1);
        bool first = true;
        for (int i = 0; i < map->table.capacity; i++) {
            Entry* entry = &map->table.entries[i];
            if (IS_EMPTY(entry->key) || IS_TOMBSTONE(entry->key)) continue;
            if (!first) bufAppend(buf, ", ", 2);
            first = false;
            bufValue(buf, entry->key, depth + 1, true);
            bufAppend(buf, ": ", 2);
            bufValue(buf, entry->value, depth + 1, true);
        }
        bufAppend(buf, "}", 1);
    } else if (IS_OBJ(value)) {
        switch (OBJ_TYPE(value)) {
            case OBJ_FUNCTION: {
                ObjFunction* fn = AS_FUNCTION(value);
                int n = snprintf(scratch, sizeof(scratch), "<fn %s>",
                                 fn->name ? fn->name->chars : "<script>");
                bufAppend(buf, scratch, n);
                break;
            }
            case OBJ_CLOSURE: {
                ObjFunction* fn = AS_CLOSURE(value)->function;
                int n = snprintf(scratch, sizeof(scratch), "<fn %s>",
                                 fn->name ? fn->name->chars : "<script>");
                bufAppend(buf, scratch, n);
                break;
            }
            case OBJ_NATIVE: {
                int n = snprintf(scratch, sizeof(scratch), "<native fn %s>",
                                 AS_NATIVE(value)->name);
                bufAppend(buf, scratch, n);
                break;
            }
            default:
                bufAppend(buf, "<obj>", 5);
                break;
        }
    }
}

static ObjString* stringifyValue(Value value) {
    if (IS_STRING(value)) return AS_STRING(value);
    Buffer buf = {NULL, 0, 0};
    bufValue(&buf, value, 0, false);
    ObjString* result = copyString(buf.data != NULL ? buf.data : "", buf.length);
    free(buf.data);
    return result;
}

// ----------------------------------------------------------------- natives

static bool nativeError(Value* result, const char* format, ...) {
    char message[192];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    *result = OBJ_VAL(copyString(message, (int)strlen(message)));
    return false;
}

static bool printNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    printValue(args[0]);
    printf("\n");
    *result = NIL_VAL;
    return true;
}

static bool clockNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    (void)args;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *result = NUM_VAL((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
    return true;
}

static bool lenNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    Value v = args[0];
    if (IS_STRING(v)) {
        *result = NUM_VAL(AS_STRING(v)->length);
    } else if (IS_ARRAY(v)) {
        *result = NUM_VAL(AS_ARRAY(v)->items.count);
    } else if (IS_MAP(v)) {
        *result = NUM_VAL(AS_MAP(v)->table.liveCount);
    } else {
        return nativeError(result, "len() expects a string, array, or map "
                                   "(got a %s value).", valueTypeName(v));
    }
    return true;
}

static bool pushNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return nativeError(result, "push() expects an array (got a %s value).",
                           valueTypeName(args[0]));
    }
    writeValueArray(&AS_ARRAY(args[0])->items, args[1]);
    *result = args[0];
    return true;
}

static bool popNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return nativeError(result, "pop() expects an array (got a %s value).",
                           valueTypeName(args[0]));
    }
    ObjArray* array = AS_ARRAY(args[0]);
    if (array->items.count == 0) {
        return nativeError(result, "pop() on an empty array.");
    }
    *result = array->items.values[--array->items.count];
    return true;
}

static bool getNative(int argCount, Value* args, Value* result) {
    Value fallback = argCount == 3 ? args[2] : NIL_VAL;
    if (IS_MAP(args[0])) {
        Value key = args[1];
        if ((!IS_STRING(key) && !IS_NUM(key)) ||
            (IS_NUM(key) && isnan(AS_NUM(key))) ||
            !tableGet(&AS_MAP(args[0])->table, key, result)) {
            *result = fallback;
        }
        return true;
    }
    if (IS_ARRAY(args[0])) {
        ObjArray* array = AS_ARRAY(args[0]);
        Value key = args[1];
        if (IS_NUM(key) && isInt(AS_NUM(key)) && AS_NUM(key) >= 0 &&
            AS_NUM(key) < array->items.count) {
            *result = array->items.values[(int)AS_NUM(key)];
        } else {
            *result = fallback;
        }
        return true;
    }
    return nativeError(result, "get() expects a map or array (got a %s "
                               "value).", valueTypeName(args[0]));
}

static bool delNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_MAP(args[0])) {
        return nativeError(result, "del() expects a map (got a %s value).",
                           valueTypeName(args[0]));
    }
    Value key = args[1];
    if (!IS_STRING(key) && !IS_NUM(key)) {
        *result = FALSE_VAL;
        return true;
    }
    *result = BOOL_VAL(tableDelete(&AS_MAP(args[0])->table, key));
    return true;
}

static bool keysNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_MAP(args[0])) {
        return nativeError(result, "keys() expects a map (got a %s value).",
                           valueTypeName(args[0]));
    }
    ObjArray* array = newArray();
    pushTempRoot(OBJ_VAL(array)); // keep alive while items grow
    Table* table = &AS_MAP(args[0])->table;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_EMPTY(entry->key) || IS_TOMBSTONE(entry->key)) continue;
        writeValueArray(&array->items, entry->key);
    }
    popTempRoot();
    *result = OBJ_VAL(array);
    return true;
}

static bool strNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    *result = OBJ_VAL(stringifyValue(args[0]));
    return true;
}

static bool numNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    Value v = args[0];
    if (IS_NUM(v)) {
        *result = v;
        return true;
    }
    if (IS_STRING(v)) {
        const char* chars = AS_CSTRING(v);
        char* end;
        double value = strtod(chars, &end);
        while (*end == ' ' || *end == '\t') end++;
        if (end != chars && *end == '\0') {
            *result = NUM_VAL(value);
        } else {
            *result = NIL_VAL;
        }
        return true;
    }
    *result = NIL_VAL;
    return true;
}

static bool chrNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_NUM(args[0]) || !isInt(AS_NUM(args[0])) || AS_NUM(args[0]) < 0 ||
        AS_NUM(args[0]) > 255) {
        return nativeError(result, "chr() expects an integer in 0..256.");
    }
    char c = (char)(int)AS_NUM(args[0]);
    *result = OBJ_VAL(copyString(&c, 1));
    return true;
}

static bool ordNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_STRING(args[0]) || AS_STRING(args[0])->length != 1) {
        return nativeError(result, "ord() expects a 1-character string.");
    }
    *result = NUM_VAL((uint8_t)AS_CSTRING(args[0])[0]);
    return true;
}

static bool floorNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_NUM(args[0])) {
        return nativeError(result, "floor() expects a number (got a %s "
                                   "value).", valueTypeName(args[0]));
    }
    *result = NUM_VAL(floor(AS_NUM(args[0])));
    return true;
}

static bool sqrtNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    if (!IS_NUM(args[0])) {
        return nativeError(result, "sqrt() expects a number (got a %s "
                                   "value).", valueTypeName(args[0]));
    }
    *result = NUM_VAL(sqrt(AS_NUM(args[0])));
    return true;
}

static bool typeofNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    const char* name = valueTypeName(args[0]);
    *result = OBJ_VAL(copyString(name, (int)strlen(name)));
    return true;
}

static bool assertNative(int argCount, Value* args, Value* result) {
    if (!isFalsey(args[0])) {
        *result = NIL_VAL;
        return true;
    }
    if (argCount == 2 && IS_STRING(args[1])) {
        return nativeError(result, "assertion failed: %s",
                           AS_CSTRING(args[1]));
    }
    return nativeError(result, "assertion failed.");
}

static bool readlineNative(int argCount, Value* args, Value* result) {
    (void)argCount;
    (void)args;
    Buffer buf = {NULL, 0, 0};
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        char ch = (char)c;
        bufAppend(&buf, &ch, 1);
    }
    if (c == EOF && buf.length == 0) {
        free(buf.data);
        *result = NIL_VAL;
        return true;
    }
    *result = OBJ_VAL(copyString(buf.data != NULL ? buf.data : "", buf.length));
    free(buf.data);
    return true;
}

static void defineNative(const char* name, NativeFn function, int minArity,
                         int maxArity) {
    ObjString* nameString = copyString(name, (int)strlen(name));
    pushTempRoot(OBJ_VAL(nameString));
    ObjNative* native = newNative(function, name, minArity, maxArity);
    pushTempRoot(OBJ_VAL(native));
    int index = vmGlobalIndex(nameString);
    vm.globals.values[index] = OBJ_VAL(native);
    popTempRoot();
    popTempRoot();
}

// --------------------------------------------------------------- VM setup

void initVM(void) {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    vm.replMode = false;
    vm.dumpBytecode = false;
    vm.scriptPath = "<script>";

    const char* stress = getenv("BEAST_GC_STRESS");
    vm.gcStress = stress != NULL && stress[0] != '\0' && stress[0] != '0';

    initValueArray(&vm.globals);
    initValueArray(&vm.globalNames);
    initTable(&vm.globalNameTable);
    initTable(&vm.strings);

    defineNative("print", printNative, 1, 1);
    defineNative("clock", clockNative, 0, 0);
    defineNative("len", lenNative, 1, 1);
    defineNative("push", pushNative, 2, 2);
    defineNative("pop", popNative, 1, 1);
    defineNative("get", getNative, 2, 3);
    defineNative("del", delNative, 2, 2);
    defineNative("keys", keysNative, 1, 1);
    defineNative("str", strNative, 1, 1);
    defineNative("num", numNative, 1, 1);
    defineNative("chr", chrNative, 1, 1);
    defineNative("ord", ordNative, 1, 1);
    defineNative("floor", floorNative, 1, 1);
    defineNative("sqrt", sqrtNative, 1, 1);
    defineNative("typeof", typeofNative, 1, 1);
    defineNative("assert", assertNative, 1, 2);
    defineNative("readline", readlineNative, 0, 0);
}

void freeVM(void) {
    freeValueArray(&vm.globals);
    freeValueArray(&vm.globalNames);
    freeTable(&vm.globalNameTable);
    freeTable(&vm.strings);
    freeObjects();
}

// ------------------------------------------------------------------ calls

static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("%s() expects %d argument%s, got %d.",
                     closure->function->name != NULL
                         ? closure->function->name->chars
                         : "(anon)",
                     closure->function->arity,
                     closure->function->arity == 1 ? "" : "s", argCount);
        return false;
    }
    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow (too much recursion).");
        return false;
    }
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                ObjNative* native = AS_NATIVE(callee);
                if (argCount < native->minArity ||
                    (native->maxArity != -1 &&
                     argCount > native->maxArity)) {
                    if (native->minArity == native->maxArity) {
                        runtimeError("%s() expects %d argument%s, got %d.",
                                     native->name, native->minArity,
                                     native->minArity == 1 ? "" : "s",
                                     argCount);
                    } else {
                        runtimeError(
                            "%s() expects %d to %d arguments, got %d.",
                            native->name, native->minArity, native->maxArity,
                            argCount);
                    }
                    return false;
                }
                Value result;
                if (!native->function(argCount,
                                      vm.stackTop - argCount, &result)) {
                    runtimeError("%s", IS_STRING(result)
                                           ? AS_CSTRING(result)
                                           : "native call failed.");
                    return false;
                }
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions (got a %s value).",
                 valueTypeName(callee));
    return false;
}

static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void concatenate(void) {
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));
    int length = a->length + b->length;
    char* chars = (char*)malloc(length + 1);
    if (chars == NULL) exit(74);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
    ObjString* result = copyString(chars, length);
    free(chars);
    pop();
    pop();
    push(OBJ_VAL(result));
}

// -------------------------------------------------------------- run loop

static InterpretResult run(void) {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];
    register uint8_t* ip = frame->ip;

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define RUNTIME_ERROR(...)                    \
    do {                                      \
        frame->ip = ip;                       \
        runtimeError(__VA_ARGS__);            \
        return INTERPRET_RUNTIME_ERROR;       \
    } while (false)
#define CHECK_STACK(n)                                        \
    do {                                                      \
        if (vm.stackTop + (n) > vm.stack + STACK_MAX) {       \
            RUNTIME_ERROR("Stack overflow.");                 \
        }                                                     \
    } while (false)
#define BINARY_OP(valueConstructor, op, symbol)                          \
    do {                                                                 \
        if (!IS_NUM(peek(0)) || !IS_NUM(peek(1))) {                      \
            RUNTIME_ERROR("Operands of '%s' must be numbers (got %s "    \
                          "and %s).",                                    \
                          symbol, valueTypeName(peek(1)),                \
                          valueTypeName(peek(0)));                       \
        }                                                                \
        double b = AS_NUM(pop());                                        \
        double a = AS_NUM(pop());                                        \
        push(valueConstructor(a op b));                                  \
    } while (false)
#define FUSED_COMPARE(op, symbol)                                        \
    do {                                                                 \
        uint16_t offset = READ_SHORT();                                  \
        if (!IS_NUM(peek(0)) || !IS_NUM(peek(1))) {                      \
            RUNTIME_ERROR("Operands of '%s' must be numbers (got %s "    \
                          "and %s).",                                    \
                          symbol, valueTypeName(peek(1)),                \
                          valueTypeName(peek(0)));                       \
        }                                                                \
        double b = AS_NUM(pop());                                        \
        double a = AS_NUM(pop());                                        \
        if (!(a op b)) ip += offset;                                     \
    } while (false)

#ifdef BEAST_COMPUTED_GOTO

    static void* dispatchTable[] = {
        &&op_CONSTANT, &&op_NIL, &&op_TRUE, &&op_FALSE, &&op_POP,
        &&op_POPN, &&op_DUP, &&op_DUP2, &&op_GET_LOCAL, &&op_SET_LOCAL,
        &&op_GET_GLOBAL, &&op_SET_GLOBAL, &&op_DEFINE_GLOBAL,
        &&op_GET_UPVALUE, &&op_SET_UPVALUE, &&op_EQUAL, &&op_NOT_EQUAL,
        &&op_GREATER, &&op_GREATER_EQUAL, &&op_LESS, &&op_LESS_EQUAL,
        &&op_ADD, &&op_SUB, &&op_MUL, &&op_DIV, &&op_MOD, &&op_NOT,
        &&op_NEGATE, &&op_JUMP, &&op_JUMP_IF_FALSE,
        &&op_JUMP_IF_FALSE_PEEK, &&op_JUMP_IF_TRUE_PEEK, &&op_LOOP,
        &&op_JUMP_IF_NOT_LESS, &&op_JUMP_IF_NOT_LESS_EQUAL,
        &&op_JUMP_IF_NOT_GREATER, &&op_JUMP_IF_NOT_GREATER_EQUAL,
        &&op_JUMP_IF_NOT_EQUAL, &&op_JUMP_IF_EQUAL, &&op_FOR_PREP,
        &&op_FOR_RANGE, &&op_CALL, &&op_CLOSURE, &&op_CLOSE_UPVALUE,
        &&op_ARRAY, &&op_MAP, &&op_INDEX_GET, &&op_INDEX_SET, &&op_ECHO,
        &&op_RETURN,
    };

#define DISPATCH() goto* dispatchTable[READ_BYTE()]
#define CASE(name) op_##name:

    DISPATCH();

#else

#define DISPATCH() goto dispatchLoop
#define CASE(name) case OP_##name:

dispatchLoop:
    switch (READ_BYTE()) {

#endif

    CASE(CONSTANT) {
        CHECK_STACK(1);
        push(READ_CONSTANT());
        DISPATCH();
    }
    CASE(NIL) {
        CHECK_STACK(1);
        push(NIL_VAL);
        DISPATCH();
    }
    CASE(TRUE) {
        CHECK_STACK(1);
        push(TRUE_VAL);
        DISPATCH();
    }
    CASE(FALSE) {
        CHECK_STACK(1);
        push(FALSE_VAL);
        DISPATCH();
    }
    CASE(POP) {
        pop();
        DISPATCH();
    }
    CASE(POPN) {
        vm.stackTop -= READ_BYTE();
        DISPATCH();
    }
    CASE(DUP) {
        CHECK_STACK(1);
        push(peek(0));
        DISPATCH();
    }
    CASE(DUP2) {
        CHECK_STACK(2);
        push(peek(1));
        push(peek(1));
        DISPATCH();
    }
    CASE(GET_LOCAL) {
        CHECK_STACK(1);
        push(frame->slots[READ_BYTE()]);
        DISPATCH();
    }
    CASE(SET_LOCAL) {
        frame->slots[READ_BYTE()] = pop();
        DISPATCH();
    }
    CASE(GET_GLOBAL) {
        CHECK_STACK(1);
        uint16_t index = READ_SHORT();
        Value value = vm.globals.values[index];
        if (IS_UNDEFINED(value)) {
            RUNTIME_ERROR(
                "Undefined global '%s'.",
                AS_CSTRING(vm.globalNames.values[index]));
        }
        push(value);
        DISPATCH();
    }
    CASE(SET_GLOBAL) {
        uint16_t index = READ_SHORT();
        if (IS_UNDEFINED(vm.globals.values[index])) {
            RUNTIME_ERROR(
                "Undefined global '%s'; declare it with 'let %s = ...'.",
                AS_CSTRING(vm.globalNames.values[index]),
                AS_CSTRING(vm.globalNames.values[index]));
        }
        vm.globals.values[index] = pop();
        DISPATCH();
    }
    CASE(DEFINE_GLOBAL) {
        vm.globals.values[READ_SHORT()] = pop();
        DISPATCH();
    }
    CASE(GET_UPVALUE) {
        CHECK_STACK(1);
        push(*frame->closure->upvalues[READ_BYTE()]->location);
        DISPATCH();
    }
    CASE(SET_UPVALUE) {
        *frame->closure->upvalues[READ_BYTE()]->location = pop();
        DISPATCH();
    }
    CASE(EQUAL) {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        DISPATCH();
    }
    CASE(NOT_EQUAL) {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(!valuesEqual(a, b)));
        DISPATCH();
    }
    CASE(GREATER) {
        BINARY_OP(BOOL_VAL, >, ">");
        DISPATCH();
    }
    CASE(GREATER_EQUAL) {
        BINARY_OP(BOOL_VAL, >=, ">=");
        DISPATCH();
    }
    CASE(LESS) {
        BINARY_OP(BOOL_VAL, <, "<");
        DISPATCH();
    }
    CASE(LESS_EQUAL) {
        BINARY_OP(BOOL_VAL, <=, "<=");
        DISPATCH();
    }
    CASE(ADD) {
        if (IS_NUM(peek(0)) && IS_NUM(peek(1))) {
            double b = AS_NUM(pop());
            double a = AS_NUM(pop());
            push(NUM_VAL(a + b));
        } else if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
            concatenate();
        } else if (IS_STRING(peek(0)) || IS_STRING(peek(1))) {
            RUNTIME_ERROR("Can't add %s and %s; use str() to convert.",
                          valueTypeName(peek(1)), valueTypeName(peek(0)));
        } else {
            RUNTIME_ERROR("Operands of '+' must be two numbers or two "
                          "strings (got %s and %s).",
                          valueTypeName(peek(1)), valueTypeName(peek(0)));
        }
        DISPATCH();
    }
    CASE(SUB) {
        BINARY_OP(NUM_VAL, -, "-");
        DISPATCH();
    }
    CASE(MUL) {
        BINARY_OP(NUM_VAL, *, "*");
        DISPATCH();
    }
    CASE(DIV) {
        BINARY_OP(NUM_VAL, /, "/");
        DISPATCH();
    }
    CASE(MOD) {
        if (!IS_NUM(peek(0)) || !IS_NUM(peek(1))) {
            RUNTIME_ERROR("Operands of '%%' must be numbers (got %s and "
                          "%s).",
                          valueTypeName(peek(1)), valueTypeName(peek(0)));
        }
        double b = AS_NUM(pop());
        double a = AS_NUM(pop());
        push(NUM_VAL(fmod(a, b)));
        DISPATCH();
    }
    CASE(NOT) {
        push(BOOL_VAL(isFalsey(pop())));
        DISPATCH();
    }
    CASE(NEGATE) {
        if (!IS_NUM(peek(0))) {
            RUNTIME_ERROR("Operand of unary '-' must be a number (got a "
                          "%s value).",
                          valueTypeName(peek(0)));
        }
        push(NUM_VAL(-AS_NUM(pop())));
        DISPATCH();
    }
    CASE(JUMP) {
        uint16_t offset = READ_SHORT();
        ip += offset;
        DISPATCH();
    }
    CASE(JUMP_IF_FALSE) {
        uint16_t offset = READ_SHORT();
        if (isFalsey(pop())) ip += offset;
        DISPATCH();
    }
    CASE(JUMP_IF_FALSE_PEEK) {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) ip += offset;
        DISPATCH();
    }
    CASE(JUMP_IF_TRUE_PEEK) {
        uint16_t offset = READ_SHORT();
        if (!isFalsey(peek(0))) ip += offset;
        DISPATCH();
    }
    CASE(LOOP) {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        DISPATCH();
    }
    CASE(JUMP_IF_NOT_LESS) {
        FUSED_COMPARE(<, "<");
        DISPATCH();
    }
    CASE(JUMP_IF_NOT_LESS_EQUAL) {
        FUSED_COMPARE(<=, "<=");
        DISPATCH();
    }
    CASE(JUMP_IF_NOT_GREATER) {
        FUSED_COMPARE(>, ">");
        DISPATCH();
    }
    CASE(JUMP_IF_NOT_GREATER_EQUAL) {
        FUSED_COMPARE(>=, ">=");
        DISPATCH();
    }
    CASE(JUMP_IF_NOT_EQUAL) {
        uint16_t offset = READ_SHORT();
        Value b = pop();
        Value a = pop();
        if (!valuesEqual(a, b)) ip += offset;
        DISPATCH();
    }
    CASE(JUMP_IF_EQUAL) {
        uint16_t offset = READ_SHORT();
        Value b = pop();
        Value a = pop();
        if (valuesEqual(a, b)) ip += offset;
        DISPATCH();
    }
    CASE(FOR_PREP) {
        uint8_t slot = READ_BYTE();
        uint16_t offset = READ_SHORT();
        Value start = frame->slots[slot];
        Value limit = frame->slots[slot + 1];
        if (!IS_NUM(start) || !IS_NUM(limit)) {
            RUNTIME_ERROR("For-range bounds must be numbers (got %s and "
                          "%s).",
                          valueTypeName(start), valueTypeName(limit));
        }
        if (AS_NUM(start) < AS_NUM(limit)) {
            CHECK_STACK(1);
            push(start);
        } else {
            ip += offset;
        }
        DISPATCH();
    }
    CASE(FOR_RANGE) {
        uint8_t slot = READ_BYTE();
        uint16_t offset = READ_SHORT();
        double counter = AS_NUM(frame->slots[slot]) + 1;
        frame->slots[slot] = NUM_VAL(counter);
        if (counter < AS_NUM(frame->slots[slot + 1])) {
            push(NUM_VAL(counter)); // reuses the slot freed just before
            ip -= offset;
        }
        DISPATCH();
    }
    CASE(CALL) {
        int argCount = READ_BYTE();
        frame->ip = ip;
        if (!callValue(peek(argCount), argCount)) {
            return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        DISPATCH();
    }
    CASE(CLOSURE) {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        CHECK_STACK(1);
        push(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
            uint8_t isLocal = READ_BYTE();
            uint8_t index = READ_BYTE();
            if (isLocal) {
                closure->upvalues[i] =
                    captureUpvalue(frame->slots + index);
            } else {
                closure->upvalues[i] = frame->closure->upvalues[index];
            }
        }
        DISPATCH();
    }
    CASE(CLOSE_UPVALUE) {
        closeUpvalues(vm.stackTop - 1);
        pop();
        DISPATCH();
    }
    CASE(ARRAY) {
        uint16_t count = READ_SHORT();
        ObjArray* array = newArray();
        pushTempRoot(OBJ_VAL(array));
        for (int i = 0; i < count; i++) {
            writeValueArray(&array->items, vm.stackTop[-count + i]);
        }
        vm.stackTop -= count;
        CHECK_STACK(1);
        push(OBJ_VAL(array));
        popTempRoot();
        DISPATCH();
    }
    CASE(MAP) {
        uint16_t count = READ_SHORT();
        ObjMap* map = newMap();
        pushTempRoot(OBJ_VAL(map));
        for (int i = 0; i < count; i++) {
            Value key = vm.stackTop[-2 * (count - i)];
            Value value = vm.stackTop[-2 * (count - i) + 1];
            if (IS_NUM(key)) {
                if (isnan(AS_NUM(key))) {
                    popTempRoot();
                    RUNTIME_ERROR("Map keys can't be NaN.");
                }
                if (AS_NUM(key) == 0) key = NUM_VAL(0); // fold -0 to 0
            } else if (!IS_STRING(key)) {
                popTempRoot();
                RUNTIME_ERROR("Map keys must be strings or numbers (got a "
                              "%s value).",
                              valueTypeName(key));
            }
            tableSet(&map->table, key, value);
        }
        vm.stackTop -= 2 * count;
        CHECK_STACK(1);
        push(OBJ_VAL(map));
        popTempRoot();
        DISPATCH();
    }
    CASE(INDEX_GET) {
        Value key = peek(0);
        Value container = peek(1);
        if (IS_ARRAY(container)) {
            ObjArray* array = AS_ARRAY(container);
            if (!IS_NUM(key) || !isInt(AS_NUM(key))) {
                RUNTIME_ERROR("Array index must be an integer (got %s).",
                              IS_NUM(key) ? "a fraction"
                                          : valueTypeName(key));
            }
            double index = AS_NUM(key);
            if (index < 0 || index >= array->items.count) {
                char buf[40];
                formatNumber(buf, sizeof(buf), index);
                RUNTIME_ERROR("Array index %s is out of bounds (len %d).",
                              buf, array->items.count);
            }
            pop();
            pop();
            push(array->items.values[(int)index]);
        } else if (IS_MAP(container)) {
            if (IS_NUM(key)) {
                if (isnan(AS_NUM(key))) {
                    RUNTIME_ERROR("Map keys can't be NaN.");
                }
            } else if (!IS_STRING(key)) {
                RUNTIME_ERROR("Map keys must be strings or numbers (got a "
                              "%s value).",
                              valueTypeName(key));
            }
            Value value;
            if (!tableGet(&AS_MAP(container)->table, key, &value)) {
                value = NIL_VAL; // missing keys read as nil
            }
            pop();
            pop();
            push(value);
        } else if (IS_STRING(container)) {
            ObjString* string = AS_STRING(container);
            if (!IS_NUM(key) || !isInt(AS_NUM(key))) {
                RUNTIME_ERROR("String index must be an integer (got %s).",
                              IS_NUM(key) ? "a fraction"
                                          : valueTypeName(key));
            }
            double index = AS_NUM(key);
            if (index < 0 || index >= string->length) {
                char buf[40];
                formatNumber(buf, sizeof(buf), index);
                RUNTIME_ERROR("String index %s is out of bounds (len %d).",
                              buf, string->length);
            }
            ObjString* ch = copyString(&string->chars[(int)index], 1);
            pop();
            pop();
            push(OBJ_VAL(ch));
        } else {
            RUNTIME_ERROR("Can't index a %s value.",
                          valueTypeName(container));
        }
        DISPATCH();
    }
    CASE(INDEX_SET) {
        Value value = peek(0);
        Value key = peek(1);
        Value container = peek(2);
        if (IS_ARRAY(container)) {
            ObjArray* array = AS_ARRAY(container);
            if (!IS_NUM(key) || !isInt(AS_NUM(key))) {
                RUNTIME_ERROR("Array index must be an integer (got %s).",
                              IS_NUM(key) ? "a fraction"
                                          : valueTypeName(key));
            }
            double index = AS_NUM(key);
            if (index < 0 || index >= array->items.count) {
                char buf[40];
                formatNumber(buf, sizeof(buf), index);
                RUNTIME_ERROR("Array index %s is out of bounds (len %d); "
                              "use push() to grow an array.",
                              buf, array->items.count);
            }
            array->items.values[(int)index] = value;
        } else if (IS_MAP(container)) {
            if (IS_NUM(key)) {
                if (isnan(AS_NUM(key))) {
                    RUNTIME_ERROR("Map keys can't be NaN.");
                }
                if (AS_NUM(key) == 0) key = NUM_VAL(0); // fold -0 to 0
            } else if (!IS_STRING(key)) {
                RUNTIME_ERROR("Map keys must be strings or numbers (got a "
                              "%s value).",
                              valueTypeName(key));
            }
            tableSet(&AS_MAP(container)->table, key, value);
        } else if (IS_STRING(container)) {
            RUNTIME_ERROR("Strings are immutable.");
        } else {
            RUNTIME_ERROR("Can't index a %s value.",
                          valueTypeName(container));
        }
        vm.stackTop -= 3;
        DISPATCH();
    }
    CASE(ECHO) {
        Value value = pop();
        if (!IS_NIL(value)) {
            printValue(value);
            printf("\n");
        }
        DISPATCH();
    }
    CASE(RETURN) {
        Value result = pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
            pop(); // the script closure
            return INTERPRET_OK;
        }
        vm.stackTop = frame->slots;
        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        DISPATCH();
    }

#ifndef BEAST_COMPUTED_GOTO
    default:
        RUNTIME_ERROR("Unknown opcode.");
    }
#endif

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef RUNTIME_ERROR
#undef CHECK_STACK
#undef BINARY_OP
#undef FUSED_COMPARE
#undef DISPATCH
#undef CASE
}

InterpretResult interpret(const char* source, const char* path) {
    vm.scriptPath = path;
    ObjFunction* function = compile(source, path, vm.replMode);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    if (vm.dumpBytecode) {
        pushTempRoot(OBJ_VAL(function));
        disassembleFunction(function);
        popTempRoot();
        return INTERPRET_OK;
    }

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
    return run();
}
