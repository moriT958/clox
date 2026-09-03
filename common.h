#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// UINT8_MAXは最大インデックス255なので、0番を含む要素数256にするため1を足す。
#define UINT8_COUNT (UINT8_MAX + 1)

#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION

#endif // !clox_common_h
