#include "chunk.h"
#include "debug.h"

#include "common.h"
#include "vm.h"

int main(int argc, const char *argv[]) {
    /*
     * 1. VM を初期化する
     */
    initVM();

    /*
     * 2. チャンクを手動で組み立てる
     */
    Chunk chunk;
    initChunk(&chunk);
    // 定数プールに 1.2 を追加する (定数プール配列の index が返る)
    int constant = addConstant(&chunk, 1.2);
    // 定数ロード命令をチャンクに書き込む (オペコード+オペランド)
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 123);
    // 符号反転の命令を書き込む
    writeChunk(&chunk, OP_NAGATE, 123);
    // リターン命令をチャンクに書き込む
    writeChunk(&chunk, OP_RETURN, 123);

    // チャンクの中身をデバッグ表示
    // disassembleChunk(&chunk, "test chunk");

    /*
     * 3. VM でチャンクを実行
     */
    interpret(&chunk);

    /*
     * 4. VM と チャンクを片付ける
     */
    freeVM();
    freeChunk(&chunk);

    return 0;
}
