/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "io.h"

static uint8_t *data_memory_base;
#define DATA_MEM_SIZE 0x100000000ULL

void memory_new()
{
    data_memory_base =
        (uint8_t *) mmap(NULL, DATA_MEM_SIZE, PROT_READ | PROT_WRITE,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (data_memory_base == MAP_FAILED)
        fprintf(stderr, "warning: %s:%d [TODO] %s\n", __FILE__, __LINE__,
                "MMAP FAIL");
}

void memory_delete()
{
    munmap(data_memory_base, DATA_MEM_SIZE);
}

void memory_read(uint8_t *dst, uint32_t addr, uint32_t size)
{
    memcpy(dst, data_memory_base + addr, size);
}

uint32_t memory_read_str(uint8_t *dst, uint32_t addr, uint32_t max)
{
    uint32_t len = 0;
    const uint8_t *end = dst + max;
    for (;; ++len, ++dst) {
        uint8_t ch = 0;
        memory_read(&ch, addr + len, 1);
        if (dst < end)
            *dst = ch;
        if (!ch)
            break;
    }
    return len + 1;
}

uint32_t memory_ifetch(uint32_t addr)
{
    return *(const uint32_t *) (data_memory_base + addr);
}

uint32_t memory_read_w(uint32_t addr)
{
    return *(uint32_t *) (data_memory_base + addr);
}

uint16_t memory_read_s(uint32_t addr)
{
    return *(uint16_t *) (data_memory_base + addr);
}

uint8_t memory_read_b(uint32_t addr)
{
    return *(uint8_t *) (data_memory_base + addr);
}

void memory_write(uint32_t addr, const uint8_t *src, uint32_t size)
{
    memcpy(data_memory_base + addr, src, size);
}

#define MEM_WRITE_IMPL(size, type)                                 \
    void memory_write_##size(uint32_t addr, const uint8_t *src)    \
    {                                                              \
        *(type *) (data_memory_base + addr) = *(const type *) src; \
    }

MEM_WRITE_IMPL(w, uint32_t);
MEM_WRITE_IMPL(s, uint16_t);
MEM_WRITE_IMPL(b, uint8_t);

void memory_fill(uint32_t addr, uint32_t size, uint8_t val)
{
    memset(data_memory_base + addr, val, size);
}
