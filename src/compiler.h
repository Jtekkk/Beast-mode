#ifndef beast_compiler_h
#define beast_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction* compile(const char* source, const char* path, bool replMode);
void markCompilerRoots(void);

#endif
