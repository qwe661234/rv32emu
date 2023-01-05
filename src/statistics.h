#include <stdint.h>
#include "map.h"
#include "riscv_private.h"
typedef struct info_node {
    uint32_t pc_start;
    uint32_t access_time;
    char *insn_sequence;
} info_node_t;

void info_stat(map_t map, block_t *block);

void info_print(map_t map);

extern uint64_t cache_miss;