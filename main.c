#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

// チャンクを組み立てるサンプルコード
// static void execChunk() {
//     /*
//      * 1. VM を初期化する
//      */
//     initVM();
//
//     /*
//      * 2. チャンクを手動で組み立てる
//      */
//     Chunk chunk;
//     initChunk(&chunk);
//     // 定数プールに 1.2 を追加する (定数プール配列の index が返る)
//     int constant = addConstant(&chunk, 1.2);
//     // 定数ロード命令をチャンクに書き込む (オペコード+オペランド)
//     writeChunk(&chunk, OP_CONSTANT, 123);
//     writeChunk(&chunk, constant, 123);
//     // 符号反転の命令を書き込む
//     writeChunk(&chunk, OP_NAGATE, 123);
//
//     // 足し算をやる (定数命令と ADD 命令をチャンクに追加)
//     // -1.2 + 1.2
//     int num2 = addConstant(&chunk, 1.2);
//     writeChunk(&chunk, OP_CONSTANT, 123);
//     writeChunk(&chunk, num2, 123);
//     writeChunk(&chunk, OP_ADD, 123);
//
//     // リターン命令をチャンクに書き込む
//     writeChunk(&chunk, OP_RETURN, 123);
//
//     // チャンクの中身をデバッグ表示
//     disassembleChunk(&chunk, "test chunk");
//
//     /*
//      * 3. VM でチャンクを実行
//      */
//     interpret(&chunk);
//
//     /*
//      * 4. VM と チャンクを片付ける
//      */
//     freeVM();
//     freeChunk(&chunk);
// }

static void repl() {
    char line[1024];
    for (;;) {
	printf("> ");

	if (!fgets(line, sizeof(line), stdin)) {
	    printf("\n");
	    break;
	}

	interpret(line);
    }
}

static char *readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
	fprintf(stderr, "Could not open file \"%s\".\n", path);
	exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(fileSize + 1);
    if (buffer == NULL) {
	fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
	exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
	fprintf(stderr, "Counld not read file\"%s\".\n", path);
	exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void runFile(const char *path) {
    char *source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR)
	exit(65);
    if (result == INTERPRET_RUNTIME_ERROR)
	exit(70);
}

int main(int argc, const char *argv[]) {
    initVM();

    if (argc == 1) {
	repl();
    } else if (argc == 2) {
	runFile(argv[1]);
    } else {
	fprintf(stderr, "Usage: clox [path]\n");
	exit(64);
    }

    freeVM();
    return 0;
}
