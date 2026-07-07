#include "debug.h"

#include <stdio.h>

#include "value.h"

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
    printf("%-24s %4d\n", name, chunk->code[offset + 1]);
    return offset + 2;
}

static int shortInstruction(const char* name, Chunk* chunk, int offset) {
    uint16_t operand =
        (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
    printf("%-24s %4d\n", name, operand);
    return offset + 3;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint16_t constant =
        (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
    printf("%-24s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 3;
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk,
                           int offset) {
    uint16_t jump =
        (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
    printf("%-24s %4d -> %d\n", name, offset,
           offset + 3 + sign * (int)jump);
    return offset + 3;
}

static int forInstruction(const char* name, int sign, Chunk* chunk,
                          int offset) {
    uint8_t slot = chunk->code[offset + 1];
    uint16_t jump =
        (uint16_t)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]);
    printf("%-24s slot %d, %d -> %d\n", name, slot, offset,
           offset + 4 + sign * (int)jump);
    return offset + 4;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    int line = getLine(chunk, offset);
    if (offset > 0 && line == getLine(chunk, offset - 1)) {
        printf("   | ");
    } else {
        printf("%4d ", line);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_NIL: return simpleInstruction("OP_NIL", offset);
        case OP_TRUE: return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE: return simpleInstruction("OP_FALSE", offset);
        case OP_POP: return simpleInstruction("OP_POP", offset);
        case OP_POPN: return byteInstruction("OP_POPN", chunk, offset);
        case OP_DUP: return simpleInstruction("OP_DUP", offset);
        case OP_DUP2: return simpleInstruction("OP_DUP2", offset);
        case OP_GET_LOCAL:
            return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:
            return shortInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return shortInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return shortInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_UPVALUE:
            return byteInstruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
            return byteInstruction("OP_SET_UPVALUE", chunk, offset);
        case OP_EQUAL: return simpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL:
            return simpleInstruction("OP_NOT_EQUAL", offset);
        case OP_GREATER: return simpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL:
            return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_LESS: return simpleInstruction("OP_LESS", offset);
        case OP_LESS_EQUAL:
            return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_ADD: return simpleInstruction("OP_ADD", offset);
        case OP_SUB: return simpleInstruction("OP_SUB", offset);
        case OP_MUL: return simpleInstruction("OP_MUL", offset);
        case OP_DIV: return simpleInstruction("OP_DIV", offset);
        case OP_MOD: return simpleInstruction("OP_MOD", offset);
        case OP_NOT: return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE: return simpleInstruction("OP_NEGATE", offset);
        case OP_JUMP: return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP_IF_FALSE_PEEK:
            return jumpInstruction("OP_JUMP_IF_FALSE_PEEK", 1, chunk,
                                   offset);
        case OP_JUMP_IF_TRUE_PEEK:
            return jumpInstruction("OP_JUMP_IF_TRUE_PEEK", 1, chunk, offset);
        case OP_LOOP: return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_JUMP_IF_NOT_LESS:
            return jumpInstruction("OP_JUMP_IF_NOT_LESS", 1, chunk, offset);
        case OP_JUMP_IF_NOT_LESS_EQUAL:
            return jumpInstruction("OP_JUMP_IF_NOT_LESS_EQUAL", 1, chunk,
                                   offset);
        case OP_JUMP_IF_NOT_GREATER:
            return jumpInstruction("OP_JUMP_IF_NOT_GREATER", 1, chunk,
                                   offset);
        case OP_JUMP_IF_NOT_GREATER_EQUAL:
            return jumpInstruction("OP_JUMP_IF_NOT_GREATER_EQUAL", 1, chunk,
                                   offset);
        case OP_JUMP_IF_NOT_EQUAL:
            return jumpInstruction("OP_JUMP_IF_NOT_EQUAL", 1, chunk, offset);
        case OP_JUMP_IF_EQUAL:
            return jumpInstruction("OP_JUMP_IF_EQUAL", 1, chunk, offset);
        case OP_FOR_PREP:
            return forInstruction("OP_FOR_PREP", 1, chunk, offset);
        case OP_FOR_RANGE:
            return forInstruction("OP_FOR_RANGE", -1, chunk, offset);
        case OP_CALL: return byteInstruction("OP_CALL", chunk, offset);
        case OP_CLOSURE: {
            uint16_t constant = (uint16_t)((chunk->code[offset + 1] << 8) |
                                           chunk->code[offset + 2]);
            offset += 3;
            printf("%-24s %4d ", "OP_CLOSURE", constant);
            printValue(chunk->constants.values[constant]);
            printf("\n");
            ObjFunction* function =
                AS_FUNCTION(chunk->constants.values[constant]);
            for (int j = 0; j < function->upvalueCount; j++) {
                int isLocal = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d      |                     %s %d\n",
                       offset - 2, isLocal ? "local" : "upvalue", index);
            }
            return offset;
        }
        case OP_CLOSE_UPVALUE:
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_ARRAY: return shortInstruction("OP_ARRAY", chunk, offset);
        case OP_MAP: return shortInstruction("OP_MAP", chunk, offset);
        case OP_INDEX_GET:
            return simpleInstruction("OP_INDEX_GET", offset);
        case OP_INDEX_SET:
            return simpleInstruction("OP_INDEX_SET", offset);
        case OP_ECHO: return simpleInstruction("OP_ECHO", offset);
        case OP_RETURN: return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

void disassembleFunction(ObjFunction* function) {
    disassembleChunk(&function->chunk, function->name != NULL
                                           ? function->name->chars
                                           : "<script>");
    // Recurse into nested functions.
    for (int i = 0; i < function->chunk.constants.count; i++) {
        Value constant = function->chunk.constants.values[i];
        if (IS_FUNCTION(constant)) {
            printf("\n");
            disassembleFunction(AS_FUNCTION(constant));
        }
    }
}
