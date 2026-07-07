#ifndef beast_vm_h
#define beast_vm_h

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;

    // Globals are a flat array indexed by compile-time slot; names are kept
    // in parallel for error messages. globalNameTable maps name -> index.
    ValueArray globals;
    ValueArray globalNames;
    Table globalNameTable;

    Table strings; // weak intern set
    ObjUpvalue* openUpvalues;

    size_t bytesAllocated;
    size_t nextGC;
    Obj* objects;
    int grayCount;
    int grayCapacity;
    Obj** grayStack;

    Value tempRoots[64];
    int tempRootCount;

    bool gcStress;
    bool replMode;
    bool dumpBytecode;
    const char* scriptPath;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM(void);
void freeVM(void);
InterpretResult interpret(const char* source, const char* path);
void push(Value value);
Value pop(void);

// Returns the index of a global slot for `name`, creating it (UNDEFINED)
// if needed. Used by the compiler.
int vmGlobalIndex(ObjString* name);

#endif
