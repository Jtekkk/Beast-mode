#ifndef beast_chunk_h
#define beast_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,      // u16 constant index
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_POPN,          // u8 count
    OP_DUP,
    OP_DUP2,
    OP_GET_LOCAL,     // u8 slot
    OP_SET_LOCAL,     // u8 slot (pops)
    OP_GET_GLOBAL,    // u16 global index
    OP_SET_GLOBAL,    // u16 global index (pops)
    OP_DEFINE_GLOBAL, // u16 global index (pops)
    OP_GET_UPVALUE,   // u8 index
    OP_SET_UPVALUE,   // u8 index (pops)
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NOT,
    OP_NEGATE,
    OP_JUMP,            // u16 forward offset
    OP_JUMP_IF_FALSE,   // u16 forward offset (pops condition)
    OP_JUMP_IF_FALSE_PEEK, // u16 (leaves value: 'and')
    OP_JUMP_IF_TRUE_PEEK,  // u16 (leaves value: 'or')
    OP_LOOP,            // u16 backward offset
    // Fused compare-and-branch: pop two numbers, jump when the comparison
    // does NOT hold (they replace compare + OP_JUMP_IF_FALSE).
    OP_JUMP_IF_NOT_LESS,          // u16
    OP_JUMP_IF_NOT_LESS_EQUAL,    // u16
    OP_JUMP_IF_NOT_GREATER,       // u16
    OP_JUMP_IF_NOT_GREATER_EQUAL, // u16
    OP_JUMP_IF_NOT_EQUAL,         // u16 (any operand types)
    OP_JUMP_IF_EQUAL,             // u16 (any operand types)
    // for i in a..b - see compiler.c for the frame layout.
    OP_FOR_PREP,  // u8 counter slot, u16 forward offset to loop exit
    OP_FOR_RANGE, // u8 counter slot, u16 backward offset to loop start
    OP_CALL,      // u8 arg count
    OP_CLOSURE,   // u16 constant index, then (isLocal, index) byte pairs
    OP_CLOSE_UPVALUE,
    OP_ARRAY,     // u16 element count
    OP_MAP,       // u16 pair count
    OP_INDEX_GET,
    OP_INDEX_SET, // stack: container key value -> (nothing)
    OP_ECHO,      // REPL only: print value unless nil
    OP_RETURN,
} OpCode;

typedef struct {
    int offset; // first bytecode offset on this line
    int line;
} LineStart;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int lineCount;
    int lineCapacity;
    LineStart* lines; // run-length encoded line info
    ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);
int getLine(Chunk* chunk, int instruction);

#endif
