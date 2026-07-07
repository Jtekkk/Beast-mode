#include "table.h"

#include <string.h>

#include "memory.h"
#include "object.h"

#define TABLE_MAX_LOAD_NUM 3
#define TABLE_MAX_LOAD_DEN 4

void initTable(Table* table) {
    table->count = 0;
    table->liveCount = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, Value key) {
    uint32_t index = hashValue(key) & (capacity - 1);
    Entry* tombstone = NULL;
    for (;;) {
        Entry* entry = &entries[index];
        if (IS_EMPTY(entry->key)) {
            return tombstone != NULL ? tombstone : entry;
        } else if (IS_TOMBSTONE(entry->key)) {
            if (tombstone == NULL) tombstone = entry;
        } else if (valuesEqual(entry->key, key)) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = EMPTY_VAL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_EMPTY(entry->key) || IS_TOMBSTONE(entry->key)) continue;
        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool tableGet(Table* table, Value key, Value* value) {
    if (table->count == 0) return false;
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_EMPTY(entry->key) || IS_TOMBSTONE(entry->key)) return false;
    *value = entry->value;
    return true;
}

bool tableSet(Table* table, Value key, Value value) {
    if ((table->count + 1) * TABLE_MAX_LOAD_DEN >
        table->capacity * TABLE_MAX_LOAD_NUM) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = IS_EMPTY(entry->key) || IS_TOMBSTONE(entry->key);
    if (isNewKey) {
        if (IS_EMPTY(entry->key)) table->count++;
        table->liveCount++;
    }
    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, Value key) {
    if (table->count == 0) return false;
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_EMPTY(entry->key) || IS_TOMBSTONE(entry->key)) return false;
    entry->key = TOMBSTONE_VAL;
    entry->value = NIL_VAL;
    table->liveCount--;
    return true;
}

ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash) {
    if (table->count == 0) return NULL;
    uint32_t index = hash & (table->capacity - 1);
    for (;;) {
        Entry* entry = &table->entries[index];
        if (IS_EMPTY(entry->key)) {
            return NULL;
        } else if (!IS_TOMBSTONE(entry->key)) {
            ObjString* string = AS_STRING(entry->key);
            if (string->length == length && string->hash == hash &&
                memcmp(string->chars, chars, length) == 0) {
                return string;
            }
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

void tableRemoveWhite(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (!IS_EMPTY(entry->key) && !IS_TOMBSTONE(entry->key) &&
            IS_OBJ(entry->key) && !AS_OBJ(entry->key)->isMarked) {
            tableDelete(table, entry->key);
        }
    }
}

void markTable(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (!IS_EMPTY(entry->key) && !IS_TOMBSTONE(entry->key)) {
            markValue(entry->key);
            markValue(entry->value);
        }
    }
}
