#ifndef beast_value_h
#define beast_value_h

#include <string.h>

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

// A Value is a 64-bit NaN-boxed cell. Doubles are stored verbatim. Every
// non-double lives inside the quiet-NaN space: singletons (nil, true, false,
// and the table-internal empty/tombstone markers) use the low tag bits, and
// object pointers set the sign bit alongside QNAN with the 48-bit address in
// the payload.
typedef uint64_t Value;

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN ((uint64_t)0x7ffc000000000000)

#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
#define TAG_EMPTY 4     // hash table internal: empty slot
#define TAG_TOMBSTONE 5 // hash table internal: deleted slot
#define TAG_UNDEFINED 6 // globals array: declared-but-unset sentinel

#define NIL_VAL ((Value)(uint64_t)(QNAN | TAG_NIL))
#define FALSE_VAL ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define EMPTY_VAL ((Value)(uint64_t)(QNAN | TAG_EMPTY))
#define TOMBSTONE_VAL ((Value)(uint64_t)(QNAN | TAG_TOMBSTONE))
#define UNDEFINED_VAL ((Value)(uint64_t)(QNAN | TAG_UNDEFINED))
#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)

#define IS_NIL(value) ((value) == NIL_VAL)
#define IS_BOOL(value) (((value) | 1) == TRUE_VAL)
#define IS_EMPTY(value) ((value) == EMPTY_VAL)
#define IS_TOMBSTONE(value) ((value) == TOMBSTONE_VAL)
#define IS_UNDEFINED(value) ((value) == UNDEFINED_VAL)
#define IS_NUM(value) (((value) & QNAN) != QNAN)
#define IS_OBJ(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value) ((value) == TRUE_VAL)
#define AS_OBJ(value) ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

static inline double valueToNum(Value value) {
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

static inline Value numToValue(double num) {
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

#define AS_NUM(value) valueToNum(value)
#define NUM_VAL(num) numToValue(num)
#define OBJ_VAL(obj) ((Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj)))

#else

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUM,
    VAL_OBJ,
    VAL_EMPTY,
    VAL_TOMBSTONE,
    VAL_UNDEFINED,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define BOOL_VAL(b) ((Value){VAL_BOOL, {.boolean = (b)}})
#define FALSE_VAL BOOL_VAL(false)
#define TRUE_VAL BOOL_VAL(true)
#define EMPTY_VAL ((Value){VAL_EMPTY, {.number = 0}})
#define TOMBSTONE_VAL ((Value){VAL_TOMBSTONE, {.number = 0}})
#define UNDEFINED_VAL ((Value){VAL_UNDEFINED, {.number = 0}})
#define NUM_VAL(num) ((Value){VAL_NUM, {.number = (num)}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj*)(object)}})

#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NUM(value) ((value).type == VAL_NUM)
#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_EMPTY(value) ((value).type == VAL_EMPTY)
#define IS_TOMBSTONE(value) ((value).type == VAL_TOMBSTONE)
#define IS_UNDEFINED(value) ((value).type == VAL_UNDEFINED)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUM(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#endif

typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);

bool valuesEqual(Value a, Value b);
uint32_t hashValue(Value value); // strings and numbers only
// Writes value's display form to buf; printValue and str() share it.
void printValue(Value value);
int formatNumber(char* buf, size_t size, double num);
const char* valueTypeName(Value value);

#endif
