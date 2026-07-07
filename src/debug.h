#ifndef beast_debug_h
#define beast_debug_h

#include "chunk.h"
#include "object.h"

void disassembleFunction(ObjFunction* function);
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

#endif
