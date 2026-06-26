#include "chunk.h"
#include "debug.h"

#include "common.h"

int main(int argc, const char *argv[]) {
  Chunk chunk;
  initChunk(&chunk);

  int constants = addConstants(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANTS, 123);
  writeChunk(&chunk, constants, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "test chunk");

  freeChunk(&chunk);

  return 0;
}
