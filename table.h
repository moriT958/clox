#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

/*
 * ハッシュテーブルの 1 スロット。
 * key が NULL なら未使用または削除済み。
 * key と value のペアを保持する。
 */
typedef struct {
    ObjString *key;
    Value value;
} Entry;

/*
 * 文字列を key とするハッシュテーブル。
 * count は使用中のスロット数。
 * capacity は entries 配列のサイズ。
 */
typedef struct {
    int count;
    int capacity;
    Entry *entries;
} Table;

/*
 * テーブルを空の状態に初期化する
 */
void initTable(Table *table);

/*
 * テーブルが確保したメモリを解放する
 */
void freeTable(Table *table);

/*
 * key に対応する値をテーブルから取得する。
 * 存在すれば value に格納して true、存在しなければ false を返す。
 */
bool tableGet(Table *table, ObjString *key, Value *value);

/*
 * key と value をテーブルに登録する。
 * すでに存在した場合は上書きする。
 * 登録に成功した場合は true、失敗した場合は false を返す。
 */
bool tableSet(Table *table, ObjString *key, Value value);

/*
 * テーブルから key に対応するエントリを削除する。
 * 削除はエントリを tombstone としてマークするだけで、メモリは解放しない。
 */
bool tableDelete(Table *table, ObjString *key);

/*
 * chars と同じ値の文字列が既にテーブルに登録されていれば、
 * その ObjString を返す。なければ NULL を返す。
 * 返ってきた ObjString は key として使用する。
 */
ObjString *tableFindString(Table *table, const char *chars, int length,
                           uint32_t hash);

#endif
