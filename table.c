#include <string.h>

#include "memory.h"
#include "table.h"

void initTable(Table *table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table *table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static Entry *findEntry(Entry *entries, int capacity, ObjString *key) {
    uint32_t index = key->hash % capacity;
    for (;;) {
	Entry *entry = &entries[index];
	if (entry->key == key || entry->key == NULL) {
	    return entry;
	}
	index = (index + 1) % capacity;
    }
}

bool tableGet(Table *table, ObjString *key, Value *value) {
    if (table->count == 0)
	return false;

    Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL)
	return false;

    *value = entry->value;
    return true;
}
