#include "chunk.h"
#include "debug.h"

#include "common.h"

int main(int argc, const char *argv[]) {
  Chunk chunk;
  initChunk(&chunk);

  int constants = addConstants(&chunk, 1.2);
  writeChunk(&chunk, OP_RETURN);
  writeChunk(&chunk, constants);

  disassembleChunk(&chunk, "test chunk");

  freeChunk(&chunk);

  return 0;
}
