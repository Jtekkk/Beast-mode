#include "memory.h"

#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
        if (vm.gcStress) {
            collectGarbage();
        } else if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "beast: out of memory\n");
        exit(74);
    }
    return result;
}

void pushTempRoot(Value value) {
    // Fixed-size root stack; overflow is a VM bug, not a user error.
    vm.tempRoots[vm.tempRootCount++] = value;
}

void popTempRoot(void) { vm.tempRootCount--; }

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;
    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // Gray stack uses raw realloc: growing it during GC must not
        // re-enter the collector.
        vm.grayStack =
            (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
        if (vm.grayStack == NULL) {
            fprintf(stderr, "beast: out of memory (gc)\n");
            exit(74);
        }
    }
    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING:
        case OBJ_NATIVE:
            break;
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_ARRAY:
            markArray(&((ObjArray*)object)->items);
            break;
        case OBJ_MAP:
            markTable(&((ObjMap*)object)->table);
            break;
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
    }
}

static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            reallocate(object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)object;
            freeValueArray(&array->items);
            FREE(ObjArray, object);
            break;
        }
        case OBJ_MAP: {
            ObjMap* map = (ObjMap*)object;
            freeTable(&map->table);
            FREE(ObjMap, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues,
                       closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
    }
}

static void markRoots(void) {
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }
    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL;
         upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }
    markArray(&vm.globals);
    markArray(&vm.globalNames);
    markTable(&vm.globalNameTable);
    for (int i = 0; i < vm.tempRootCount; i++) {
        markValue(vm.tempRoots[i]);
    }
    markCompilerRoots();
}

static void traceReferences(void) {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep(void) {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }
            freeObject(unreached);
        }
    }
}

void collectGarbage(void) {
    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    sweep();
    size_t floor = 1024 * 1024;
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    if (vm.nextGC < floor) vm.nextGC = floor;
}

void freeObjects(void) {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    free(vm.grayStack);
    vm.grayStack = NULL;
}
