#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "c2mir/c2mir.h"
#include "cache.h"
#include "codegen.h"
#include "compile.h"
#include "elfdef.h"
#include "mir-gen.h"
#include "mir.h"
#include "utils.h"

typedef struct {
    char *code;
    size_t code_size;
    size_t curr;
} jit_item_t;

typedef struct {
    MIR_context_t ctx;
    struct c2mir_options *options;
    uint8_t debug_level;
    uint8_t optimize_level;
} riscv_jit_t;

typedef struct {
    char *name;
    void *func;
} func_obj_t;

int get_func(void *data)
{
    jit_item_t *item = data;
    return item->curr >= item->code_size ? EOF : item->code[item->curr++];
}


#define DLIST_ITEM_FOREACH(modules, item)                     \
    for (item = DLIST_HEAD(MIR_item_t, modules->items); item; \
         item = DLIST_NEXT(MIR_item_t, item))

/* Get current time in microsecnds and update csr_time register */
static inline void update_time(riscv_t *rv)
{
    struct timeval tv;
    rv_gettimeofday(&tv);

    uint64_t t = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
    rv->csr_time[0] = t & 0xFFFFFFFF;
    rv->csr_time[1] = t >> 32;
}

#if RV32_HAS(EXT_F)
#include <math.h>
#include "softfloat.h"

#if defined(__APPLE__)
static inline int isinff(float x)
{
    return __builtin_fabsf(x) == __builtin_inff();
}
static inline int isnanf(float x)
{
    return x != x;
}
#endif
#endif /* RV32_HAS(EXT_F) */

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
        return &rv->csr_time[0];
    }
    case CSR_TIMEH: { /* Upper 32 bits of time */
        update_time(rv);
        return &rv->csr_time[1];
    }
    case CSR_INSTRET: /* Number of Instructions Retired Counter */
        /* Number of Instructions Retired Counter, just use cycle */
        return (uint32_t *) (&rv->csr_cycle);
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
        out &= FFLAG_MASK;
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
        out &= FFLAG_MASK;
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
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c &= ~val;
    return out;
}
#endif

void *import_resolver(const char *name)
{
    func_obj_t func_list[] = {
        {"sign_extend_b", sign_extend_b},
        {"sign_extend_h", sign_extend_h},
#if RV32_HAS(Zicsr)
        {"csr_csrrw", csr_csrrw},
        {"csr_csrrs", csr_csrrs},
        {"csr_csrrc", csr_csrrc},
#endif
#if RV32_HAS(EXT_F)
        {"isnanf", isnanf},
        {"isinff", isinff},
        {"sqrtf", sqrtf},
        {"calc_fclass", calc_fclass},
        {"is_nan", is_nan},
        {"is_snan", is_snan},
#endif
        {NULL, NULL},
    };
    for (int i = 0; func_list[i].name; i++) {
        if (!strcmp(name, func_list[i].name))
            return func_list[i].func;
    }
    return NULL;
}

static riscv_jit_t *jit = NULL;
static jit_item_t *jit_ptr = NULL;

uint8_t *compile(riscv_t *rv)
{
    char func_name[25];
    snprintf(func_name, 25, "jit_func_%d", rv->PC);
    c2mir_init(jit->ctx);
    size_t gen_num = 0;
    MIR_gen_init(jit->ctx, gen_num);
    MIR_gen_set_optimize_level(jit->ctx, gen_num, jit->optimize_level);
    if (!c2mir_compile(jit->ctx, jit->options, get_func, jit_ptr, func_name,
                       NULL)) {
        perror("Compile failure");
        exit(EXIT_FAILURE);
    }
    MIR_module_t module =
        DLIST_TAIL(MIR_module_t, *MIR_get_module_list(jit->ctx));
    MIR_load_module(jit->ctx, module);
    MIR_link(jit->ctx, MIR_set_gen_interface, import_resolver);
    MIR_item_t func = DLIST_HEAD(MIR_item_t, module->items);
    size_t code_len = 0;
    size_t func_len = DLIST_LENGTH(MIR_item_t, module->items);
    for (size_t i = 0; i < func_len; i++, func = DLIST_NEXT(MIR_item_t, func)) {
        if (func->item_type == MIR_func_item) {
            uint32_t *tmp = (uint32_t *) func->addr;
            while (*tmp) {
                code_len += 4;
                tmp += 1;
            }
            break;
        }
    }

    MIR_gen_finish(jit->ctx);
    c2mir_finish(jit->ctx);
    return code_cache_add(rv->cache, rv->PC, func->addr, code_len, 4);
}

uint8_t *block_compile(riscv_t *rv)
{
    if (!jit) {
        jit = calloc(1, sizeof(riscv_jit_t));
        jit->options = calloc(1, sizeof(struct c2mir_options));
        jit->ctx = MIR_init();
        jit->optimize_level = 1;
    }

    if (!jit_ptr) {
        jit_ptr = malloc(sizeof(jit_item_t));
        jit_ptr->curr = 0;
        jit_ptr->code = calloc(1, 1024 * 1024);
    } else {
        jit_ptr->curr = 0;
        memset(jit_ptr->code, 0, 1024 * 1024);
    }
    trace_and_gencode(rv, jit_ptr->code);
    jit_ptr->code_size = strlen(jit_ptr->code);
    return compile(rv);
}