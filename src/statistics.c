#include "statistics.h"
#include <stdio.h>
#include <stdlib.h>
uint64_t cache_miss = 0;
info_node_t *new_info_node(block_t *block)
{
    info_node_t *info_node = malloc(sizeof(info_node_t));
    info_node->access_time = 1;
    info_node->pc_start = block->pc_start;
    info_node->insn_sequence = malloc(block->n_insn * 9 + 1);
    char *tmp = info_node->insn_sequence;
    for (uint32_t i = 0; i < block->n_insn; i++) {
        int count = sprintf(tmp, "%d", block->ir[i].opcode);
        tmp += count;
    }
    return info_node;
}

void info_stat(map_t map, block_t *block)
{
    map_iter_t it;
    map_find(map, &it, &block->pc_start);
    if (!map_at_end(map, &it)) {
        info_node_t *target = map_iter_value(&it, info_node_t *);
        target->access_time++;
    } else {
        info_node_t *newnode = new_info_node(block);
        map_insert(map, &(block->pc_start), &(newnode));
    }
}

void info_print(map_t map)
{
    printf("cache miss = %lu\n", cache_miss);
}

// void histogram_print()
// {
//     char bar[_NCOLS * 3 + 1];
//     histogram_t histogram_total = {0};

//     for (size_t i = 0; i < BLOCK_CAPACITY; i++) {
//         if (histogram[i].block_count) {
//             histogram_total.block_count += histogram[i].block_count;
//             histogram_total.block_call += histogram[i].block_call;
//         }
//     }
//     printf("Instruction |        Count        |       Invoked Times");
//     for (size_t i = 0; i < BLOCK_CAPACITY; i++) {
//         if (histogram[i].block_count) {
//             printf(
//                 "%11zu | %9zu [%6.2lf %%]| %15zu [%6.2lf %%]| %s\n",
//                 i, histogram[i].block_count,
//                 (double) histogram[i].block_count /
//                     histogram_total.block_count * 100,
//                 histogram[i].block_call,
//                 (double) histogram[i].block_call / histogram_total.block_call
//                 *
//                     100);
//         }
//     }
// }