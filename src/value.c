#include "value.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values =
            GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
    // Bit equality covers everything except NaN != NaN for numbers.
    if (IS_NUM(a) && IS_NUM(b)) return AS_NUM(a) == AS_NUM(b);
    return a == b;
#else
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NUM: return AS_NUM(a) == AS_NUM(b);
        case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(b);
        default: return a.type == b.type;
    }
#endif
}

uint32_t hashValue(Value value) {
    if (IS_OBJ(value)) {
        // Only strings are hashable objects; callers guarantee this.
        return AS_STRING(value)->hash;
    }
    // Number key: hash the normalized bit pattern (-0 folds to 0).
    double num = AS_NUM(value);
    if (num == 0) num = 0;
    uint64_t bits;
    memcpy(&bits, &num, sizeof(bits));
    // 64 -> 32 bit finalizer (splitmix-style mix).
    bits ^= bits >> 33;
    bits *= 0xff51afd7ed558ccdULL;
    bits ^= bits >> 33;
    return (uint32_t)bits;
}

int formatNumber(char* buf, size_t size, double num) {
    // Integer-style printing covers the whole exact range (2^53 < 1e16).
    if (num == floor(num) && fabs(num) < 1e16 && !isinf(num)) {
        if (num == 0) num = 0; // print -0 as 0
        return snprintf(buf, size, "%.0f", num);
    }
    return snprintf(buf, size, "%.14g", num);
}

void printValue(Value value) {
    if (IS_NUM(value)) {
        char buf[40];
        formatNumber(buf, sizeof(buf), AS_NUM(value));
        printf("%s", buf);
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_OBJ(value)) {
        printObject(value, 0);
    } else {
        printf("<internal>");
    }
}

const char* valueTypeName(Value value) {
    if (IS_NUM(value)) return "num";
    if (IS_NIL(value)) return "nil";
    if (IS_BOOL(value)) return "bool";
    if (IS_OBJ(value)) {
        switch (OBJ_TYPE(value)) {
            case OBJ_STRING: return "str";
            case OBJ_ARRAY: return "array";
            case OBJ_MAP: return "map";
            case OBJ_FUNCTION:
            case OBJ_CLOSURE:
            case OBJ_NATIVE: return "fn";
            case OBJ_UPVALUE: return "upvalue";
        }
    }
    return "internal";
}
