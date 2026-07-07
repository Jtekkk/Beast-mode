#include "object.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u; // FNV-1a
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString* allocateString(const char* chars, int length,
                                 uint32_t hash) {
    ObjString* string = (ObjString*)allocateObject(
        sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    string->hash = hash;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    // Intern: root the string while the set may grow.
    pushTempRoot(OBJ_VAL(string));
    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
    popTempRoot();
    return string;
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
    return allocateString(chars, length, hash);
}

ObjString* takeString(char* chars, int length) {
    // Strings use flexible array members, so "taking" still copies; the
    // caller frees its buffer.
    return copyString(chars, length);
}

ObjArray* newArray(void) {
    ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    initValueArray(&array->items);
    return array;
}

ObjMap* newMap(void) {
    ObjMap* map = ALLOCATE_OBJ(ObjMap, OBJ_MAP);
    initTable(&map->table);
    return map;
}

ObjFunction* newFunction(void) {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjClosure* newClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }
    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

ObjNative* newNative(NativeFn function, const char* name, int minArity,
                     int maxArity) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    native->name = name;
    native->minArity = minArity;
    native->maxArity = maxArity;
    return native;
}

#define MAX_PRINT_DEPTH 8

static void printQuoted(Value value, int depth) {
    if (IS_STRING(value)) {
        printf("\"%s\"", AS_CSTRING(value));
    } else if (IS_OBJ(value)) {
        printObject(value, depth);
    } else {
        printValue(value);
    }
}

void printObject(Value value, int depth) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_ARRAY: {
            if (depth >= MAX_PRINT_DEPTH) {
                printf("[...]");
                break;
            }
            ObjArray* array = AS_ARRAY(value);
            printf("[");
            for (int i = 0; i < array->items.count; i++) {
                if (i > 0) printf(", ");
                printQuoted(array->items.values[i], depth + 1);
            }
            printf("]");
            break;
        }
        case OBJ_MAP: {
            if (depth >= MAX_PRINT_DEPTH) {
                printf("{...}");
                break;
            }
            ObjMap* map = AS_MAP(value);
            printf("{");
            bool first = true;
            for (int i = 0; i < map->table.capacity; i++) {
                Entry* entry = &map->table.entries[i];
                if (IS_EMPTY(entry->key) || IS_TOMBSTONE(entry->key)) continue;
                if (!first) printf(", ");
                first = false;
                printQuoted(entry->key, depth + 1);
                printf(": ");
                printQuoted(entry->value, depth + 1);
            }
            printf("}");
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = AS_FUNCTION(value);
            if (function->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", function->name->chars);
            }
            break;
        }
        case OBJ_CLOSURE:
            printObject(OBJ_VAL(AS_CLOSURE(value)->function), depth);
            break;
        case OBJ_NATIVE:
            printf("<native fn %s>", AS_NATIVE(value)->name);
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
    }
}
