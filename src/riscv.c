/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "riscv_private.h"

queue_t *create_queue()
{
    queue_t *queue = (queue_t *) malloc(sizeof(queue_t));
    queue->count = 0;
    queue->front = queue->rear = NULL;
    return queue;
}

hashtable_t *create_hashtable()
{
    hashtable_t *hash = (hashtable_t *) malloc(sizeof(hashtable_t));
    hash->array = (node_t **) malloc(CAPACITY * sizeof(node_t *));
    return hash;
}

cache_t *create_cache()
{
    cache_t *cache = (cache_t *) malloc(sizeof(cache_t));
    cache->lruQueue = create_queue();
    cache->hashtable = create_hashtable();
    return cache;
}

void free_cache(cache_t *cache)
{
    free(cache->hashtable->array);
    free(cache->hashtable);
    free(cache->lruQueue);
    free(cache);
}

riscv_user_t rv_userdata(riscv_t *rv)
{
    assert(rv);
    return rv->userdata;
}

bool rv_set_pc(riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
#if RV32_HAS(EXT_C)
    if (pc & 1)
#else
    if (pc & 3)
#endif
        return false;

    rv->PC = pc;
    return true;
}

riscv_word_t rv_get_pc(riscv_t *rv)
{
    assert(rv);
    return rv->PC;
}

void rv_set_reg(riscv_t *rv, uint32_t reg, riscv_word_t in)
{
    assert(rv);
    if (reg < RV_N_REGS && reg != rv_reg_zero)
        rv->X[reg] = in;
}

riscv_word_t rv_get_reg(riscv_t *rv, uint32_t reg)
{
    assert(rv);
    if (reg < RV_N_REGS)
        return rv->X[reg];

    return ~0U;
}

riscv_t *rv_create(const riscv_io_t *io, riscv_user_t userdata)
{
    assert(io);

    riscv_t *rv = calloc(1, sizeof(riscv_t));

    /* copy over the IO interface */
    memcpy(&rv->io, io, sizeof(riscv_io_t));

    /* copy over the userdata */
    rv->userdata = userdata;

    /* initialize the block cache */
    rv->block_cache = create_cache();
    /* reset */
    rv_reset(rv, 0U);

    return rv;
}

void rv_halt(riscv_t *rv)
{
    rv->halt = true;
}

bool rv_has_halted(riscv_t *rv)
{
    return rv->halt;
}

void rv_delete(riscv_t *rv)
{
    assert(rv);
    free_cache(rv->block_cache);
    free(rv);
}

void rv_reset(riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
    memset(rv->X, 0, sizeof(uint32_t) * RV_N_REGS);

    /* set the reset address */
    rv->PC = pc;

    /* set the default stack pointer */
    rv->X[rv_reg_sp] = DEFAULT_STACK_ADDR;

    /* reset the csrs */
    rv->csr_mtvec = 0;
    rv->csr_cycle = 0;
    rv->csr_mstatus = 0;

#if RV32_HAS(EXT_F)
    /* reset float registers */
    memset(rv->F, 0, sizeof(float) * RV_N_REGS);
    rv->csr_fcsr = 0;
#endif

    rv->halt = false;
}

/* FIXME: provide real implementation */
void rv_stats(riscv_t *rv)
{
    printf("CSR cycle count: %" PRIu64 "\n", rv->csr_cycle);
}
