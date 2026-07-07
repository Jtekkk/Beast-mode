#ifndef beast_object_h
#define beast_object_h

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_ARRAY(value) isObjType(value, OBJ_ARRAY)
#define IS_MAP(value) isObjType(value, OBJ_MAP)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)

#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_ARRAY(value) ((ObjArray*)AS_OBJ(value))
#define AS_MAP(value) ((ObjMap*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative*)AS_OBJ(value))

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_MAP,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_NATIVE,
} ObjType;

struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj* next;
};

struct ObjString {
    Obj obj;
    int length;
    uint32_t hash;
    char chars[]; // flexible array member: one allocation per string
};

typedef struct {
    Obj obj;
    ValueArray items;
} ObjArray;

typedef struct {
    Obj obj;
    Table table;
} ObjMap;

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name; // NULL for the top-level script
} ObjFunction;

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

// Natives return true and set *result on success; on failure they set
// *result to an error-message string value and return false.
typedef bool (*NativeFn)(int argCount, Value* args, Value* result);

typedef struct {
    Obj obj;
    NativeFn function;
    const char* name;
    int minArity;
    int maxArity; // -1 = unlimited
} ObjNative;

ObjString* copyString(const char* chars, int length);
ObjString* takeString(char* chars, int length);
ObjArray* newArray(void);
ObjMap* newMap(void);
ObjFunction* newFunction(void);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjNative* newNative(NativeFn function, const char* name, int minArity, int maxArity);
void printObject(Value value, int depth);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
