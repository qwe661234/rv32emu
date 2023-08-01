#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "riscv.h"

static inline void tb_write8(uint8_t *tb, uint8_t v)
{
    *tb++ = v;
}

static uint32_t prologue(uint8_t *tb)
{
    uint32_t size = 0;
    // push rbp
    tb_write8(tb + size++, 0x55);
    // push rbx
    tb_write8(tb + size++, 0x53);
    // push r12
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x54);
    // push r13
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x55);
    // push r14
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x56);
    // push r15
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x57);
    // maintain sp
    tb_write8(tb + size++, 0x48);
    tb_write8(tb + size++, 0x81);
    tb_write8(tb + size++, 0xc4);
    tb_write8(tb + size++, 0x78);
    tb_write8(tb + size++, 0xfb);
    tb_write8(tb + size++, 0xff);
    tb_write8(tb + size++, 0xff);
    return size;
}

static uint32_t epilogue(uint8_t *tb)
{
    uint32_t size = 0;
    // maintain sp
    tb_write8(tb + size++, 0x48);
    tb_write8(tb + size++, 0x81);
    tb_write8(tb + size++, 0xc4);
    tb_write8(tb + size++, 0x88);
    tb_write8(tb + size++, 0x04);
    tb_write8(tb + size++, 0x00);
    tb_write8(tb + size++, 0x00);
    // pop r15
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x5f);
    // pop r14
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x5e);
    // pop r13
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x5d);
    // pop r12
    tb_write8(tb + size++, 0x41);
    tb_write8(tb + size++, 0x5c);
    // pop rbx
    tb_write8(tb + size++, 0x5b);
    // pop rbp
    tb_write8(tb + size++, 0x5d);
    // ret
    tb_write8(tb + size++, 0xc3);
    return size;
}

static uint32_t move_first_para(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x48);
    tb_write8(tb + size++, 0x8b);
    tb_write8(tb + size++, 0xef);
    return size;
}

static uint32_t move_ebx_edi(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x89);
    tb_write8(tb + size++, 0xdf);
    return size;
}

static uint32_t move_ebx_esi(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x89);
    tb_write8(tb + size++, 0xde);
    return size;
}

static uint32_t movrax(uint8_t *tb, uint32_t val)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0xb8);
    memcpy(tb + size, &val, 4);
    size += 4;
    return size;
}

static uint32_t movrbx(uint8_t *tb, uint32_t val)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0xbb);
    memcpy(tb + size, &val, 4);
    size += 4;
    return size;
}

static uint32_t movrsi(uint8_t *tb, uint32_t val)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0xbe);
    memcpy(tb + size, &val, 4);
    size += 4;
    return size;
}

static uint32_t load_ebx(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x8b);
    tb_write8(tb + size++, 0x5c);
    tb_write8(tb + size++, 0x87);
    tb_write8(tb + size++, 0x58);
    return size;
}

static uint32_t load_esi(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x8b);
    tb_write8(tb + size++, 0x74);
    tb_write8(tb + size++, 0x87);
    tb_write8(tb + size++, 0x58);
    return size;
}

static uint32_t store_ebx(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x89);
    tb_write8(tb + size++, 0x5c);
    tb_write8(tb + size++, 0x87);
    tb_write8(tb + size++, 0x58);
    return size;
}

static uint32_t add_ebx(uint8_t *tb, int val)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x81);
    tb_write8(tb + size++, 0xc3);
    memcpy(tb + size, &val, 4);
    size += 4;
    return size;
}

static uint32_t clear_eax(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0x33);
    tb_write8(tb + size++, 0xc0);
    return size;
}

static uint32_t invoke_sw(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0xff);
    tb_write8(tb + size++, 0x55);
    tb_write8(tb + size++, 0x28);
    return size;
}

static uint32_t invoke_ecall(uint8_t *tb)
{
    uint32_t size = 0;
    tb_write8(tb + size++, 0xff);
    tb_write8(tb + size++, 0x55);
    tb_write8(tb + size++, 0x40);
    return size;
}

typedef void (*func)(riscv_t *rv);