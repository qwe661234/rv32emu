/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "riscv_private.h"
#include "utils.h"

/* RISC-V exception code list */
#define RV_EXCEPTION_LIST                                       \
    _(insn_misaligned, 0)  /* Instruction address misaligned */ \
    _(illegal_insn, 2)     /* Illegal instruction */            \
    _(breakpoint, 3)       /* Breakpoint */                     \
    _(load_misaligned, 4)  /* Load address misaligned */        \
    _(store_misaligned, 6) /* Store/AMO address misaligned */   \
    _(ecall_M, 11)         /* Environment call from M-mode */

enum {
#define _(type, code) rv_exception_code##type = code,
    RV_EXCEPTION_LIST
#undef _
};

static void rv_exception_default_handler(riscv_t *rv)
{
    rv->csr_mepc += rv->compressed ? INSN_16 : INSN_32;
    rv->PC = rv->csr_mepc; /* mret */
}

#define EXCEPTION_HANDLER_IMPL(type, code)                                \
    static void rv_except_##type(riscv_t *rv, uint32_t mtval)             \
    {                                                                     \
        /* mtvec (Machine Trap-Vector Base Address Register)              \
         * mtvec[MXLEN-1:2]: vector base address                          \
         * mtvec[1:0] : vector mode                                       \
         */                                                               \
        const uint32_t base = rv->csr_mtvec & ~0x3;                       \
        const uint32_t mode = rv->csr_mtvec & 0x3;                        \
        /* mepc  (Machine Exception Program Counter)                      \
         * mtval (Machine Trap Value Register)                            \
         * mcause (Machine Cause Register): store exception code          \
         */                                                               \
        rv->csr_mepc = rv->PC;                                            \
        rv->csr_mtval = mtval;                                            \
        rv->csr_mcause = code;                                            \
        if (!rv->csr_mtvec) { /* in case CSR is not configured */         \
            rv_exception_default_handler(rv);                             \
            return;                                                       \
        }                                                                 \
        switch (mode) {                                                   \
        case 0: /* DIRECT: All exceptions set PC to base */               \
            rv->PC = base;                                                \
            break;                                                        \
        /* VECTORED: Asynchronous interrupts set PC to base + 4 * code */ \
        case 1:                                                           \
            rv->PC = base + 4 * code;                                     \
            break;                                                        \
        }                                                                 \
        /* longjmp to return false */                                     \
    }

/* RISC-V exception handlers */
#define _(type, code) EXCEPTION_HANDLER_IMPL(type, code)
RV_EXCEPTION_LIST
#undef _

/* Get current time in microsecnds and update csr_time register */
static inline void update_time(riscv_t *rv)
{
    struct timeval tv;
    rv_gettimeofday(&tv);

    uint64_t t = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
    rv->csr_time[0] = t & 0xFFFFFFFF;
    rv->csr_time[1] = t >> 32;
}

#if RV32_HAS(Zicsr)
/* get a pointer to a CSR */
static uint32_t *csr_get_ptr(riscv_t *rv, uint32_t csr)
{
    switch (csr) {
    case CSR_MSTATUS: /* Machine Status */
        return (uint32_t *) (&rv->csr_mstatus);
    case CSR_MTVEC: /* Machine Trap Handler */
        return (uint32_t *) (&rv->csr_mtvec);
    case CSR_MISA: /* Machine ISA and Extensions */
        return (uint32_t *) (&rv->csr_misa);

    /* Machine Trap Handling */
    case CSR_MSCRATCH: /* Machine Scratch Register */
        return (uint32_t *) (&rv->csr_mscratch);
    case CSR_MEPC: /* Machine Exception Program Counter */
        return (uint32_t *) (&rv->csr_mepc);
    case CSR_MCAUSE: /* Machine Exception Cause */
        return (uint32_t *) (&rv->csr_mcause);
    case CSR_MTVAL: /* Machine Trap Value */
        return (uint32_t *) (&rv->csr_mtval);
    case CSR_MIP: /* Machine Interrupt Pending */
        return (uint32_t *) (&rv->csr_mip);

    /* Machine Counter/Timers */
    case CSR_CYCLE: /* Cycle counter for RDCYCLE instruction */
        return (uint32_t *) (&rv->csr_cycle) + 0;
    case CSR_CYCLEH: /* Upper 32 bits of cycle */
        return (uint32_t *) (&rv->csr_cycle) + 1;

    /* TIME/TIMEH - very roughly about 1 ms per tick */
    case CSR_TIME: { /* Timer for RDTIME instruction */
        update_time(rv);
        return (uint32_t *) (&rv->csr_time) + 0;
    }
    case CSR_TIMEH: { /* Upper 32 bits of time */
        update_time(rv);
        return (uint32_t *) (&rv->csr_time) + 1;
    }
    case CSR_INSTRET: /* Number of Instructions Retired Counter */
        /* Number of Instructions Retired Counter, just use cycle */
        return (uint32_t *) (&rv->csr_cycle) + 0;
#if RV32_HAS(EXT_F)
    case CSR_FFLAGS:
        return (uint32_t *) (&rv->csr_fcsr);
    case CSR_FCSR:
        return (uint32_t *) (&rv->csr_fcsr);
#endif
    default:
        return NULL;
    }
}

static inline bool csr_is_writable(uint32_t csr)
{
    return csr < 0xc00;
}

/* CSRRW (Atomic Read/Write CSR) instruction atomically swaps values in the
 * CSRs and integer registers. CSRRW reads the old value of the CSR,
 * zero - extends the value to XLEN bits, then writes it to integer register rd.
 * The initial value in rs1 is written to the CSR.
 * If rd == x0, then the instruction shall not read the CSR and shall not cause
 * any of the side effects that might occur on a CSR read.
 */
static uint32_t csr_csrrw(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= 0b00000000000000000000000000011111;
#endif
    if (csr_is_writable(csr))
        *c = val;

    return out;
}

/* perform csrrs (atomic read and set) */
static uint32_t csr_csrrs(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= 0b00000000000000000000000000011111;
#endif
    if (csr_is_writable(csr))
        *c |= val;

    return out;
}

/* perform csrrc (atomic read and clear)
 * Read old value of CSR, zero-extend to XLEN bits, write to rd
 * Read value from rs1, use as bit mask to clear bits in CSR
 */
static uint32_t csr_csrrc(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= 0b00000000000000000000000000011111;
#endif
    if (csr_is_writable(csr))
        *c &= ~val;
    return out;
}
#endif

/* initialize the block map */
static void block_map_init(block_map_t *map, const uint8_t bits)
{
    map->block_capacity = 1 << bits;
    map->size = 0;
    map->map = calloc(map->block_capacity, sizeof(struct block *));
}

/* clear all block in the block map */
void block_map_clear(block_map_t *map)
{
    assert(map);
    for (uint32_t i = 0; i < map->block_capacity; i++) {
        block_t *block = map->map[i];
        if (!block)
            continue;
        free(block->ir);
        block->code_page = NULL;
        free(block);
        map->map[i] = NULL;
    }
    map->size = 0;
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

jmp_buf jmpbuffer;
void jump_to_false()
{
    longjmp(jmpbuffer, 1);
}

riscv_t *rv_create(const riscv_io_t *io, riscv_user_t userdata)
{
    assert(io);

    riscv_t *rv = calloc(1, sizeof(riscv_t));

    /* copy over the IO interface */
    memcpy(&rv->io, io, sizeof(riscv_io_t));

    /* copy over the userdata */
    rv->userdata = userdata;

    /* initialize the block map */
    block_map_init(&rv->block_map, 10);

    /* reset */
    rv_reset(rv, 0U);

    for (int i = 0; i < 1024; i++) {
        rv->code_page[i] = malloc_exec(30);
    }

    rv->exception_handler[0] = &rv_except_insn_misaligned;
    rv->exception_handler[1] = &rv_except_illegal_insn;
    rv->exception_handler[2] = &rv_except_breakpoint;
    rv->exception_handler[3] = &rv_except_load_misaligned;
    rv->exception_handler[4] = &rv_except_store_misaligned;
    rv->exception_handler[5] = &rv_except_ecall_M;
    rv->csr_handler[0] = &csr_csrrw;
    rv->csr_handler[1] = &csr_csrrs;
    rv->csr_handler[2] = &csr_csrrc;
    rv->ret_false = &jump_to_false;

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
    block_map_clear(&rv->block_map);
    free(rv->block_map.map);
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
