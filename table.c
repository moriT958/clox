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
    Entry *tombstone = NULL;

    for (;;) {
	Entry *entry = &entries[index];
	if (entry->key == NULL) {
	    if (IS_NIL(entry->value)) {
		// 未使用の空きスロット
		return tombstone != NULL ? tombstone : entry;
	    } else {
		// tombstone (削除済み) スロット。覚えておいて探索は続ける
		if (tombstone == NULL)
		    tombstone = entry;
	    }
	} else if (entry->key == key) {
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

bool tableDelete(Table *table, ObjString *key) {
    if (table->count == 0)
	return false;

    Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL)
	return false;

    // tombstone を置く
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

#define TABLE_MAX_LOAD 0.75

static void adjustCapacity(Table *table, int capacity) {
    Entry *entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
	entries[i].key = NULL;
	entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
	Entry *entry = &table->entries[i];
	if (entry->key == NULL)
	    continue;

	Entry *dest = findEntry(entries, capacity, entry->key);
	dest->key = entry->key;
	dest->value = entry->value;
	table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool tableSet(Table *table, ObjString *key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	int capacity = GROW_CAPACITY(table->capacity);
	adjustCapacity(table, capacity);
    }

    Entry *entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value))
	table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}
