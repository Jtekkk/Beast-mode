#ifndef beast_table_h
#define beast_table_h

#include "common.h"
#include "value.h"

// Open-addressed, linear-probed hash table with power-of-two capacity.
// Keys are interned strings or numbers. Backs maps, the string intern set,
// and the compiler's global-name index.
typedef struct {
    Value key; // EMPTY_VAL = never used, TOMBSTONE_VAL = deleted
    Value value;
} Entry;

typedef struct {
    int count; // live entries + tombstones
    int liveCount;
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, Value key, Value* value);
bool tableSet(Table* table, Value key, Value value); // true if key is new
bool tableDelete(Table* table, Value key);
ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash);
void tableRemoveWhite(Table* table);
void markTable(Table* table);

#endif
