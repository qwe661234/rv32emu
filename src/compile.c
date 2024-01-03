/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cache.h"
#include "compile.h"
#include "decode.h"
#include "elfdef.h"
#include "riscv_private.h"
#include "utils.h"

typedef struct {
    char *code;
    size_t code_size;
    size_t curr; /**< the current pointer to code string */
} code_string_t;

#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *) (addr), (char *) (addr) + (size));

static bool insn_is_branch(uint8_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    IIF(can_branch)(case rv_insn_##inst:, )
        RV_INSN_LIST
#undef _
        return true;
    }
    return false;
}

static uint8_t insn_len_table[] = {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = insn_len,
    RV_INSN_LIST
#undef _
};

typedef void (*gen_func_t)(riscv_t *, rv_insn_t *, char *);
static gen_func_t dispatch_table[N_TATOL_INSNS];
static char funcbuf[128] = {0};
#define GEN(...)                   \
    sprintf(funcbuf, __VA_ARGS__); \
    strcat(gencode, funcbuf);
#define UPDATE_PC(inc) GEN("  PC = %d;\n", inc)
#define NEXT_INSN(target) GEN("  goto insn_%x;\n", target)
#define END_INSN                         \
    GEN("    rv->csr_cycle = cycle;\n"); \
    GEN("    rv->PC = PC;\n");           \
    GEN("    return true;\n");
#define RVOP(inst, code)                                             \
    static void gen_##inst(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, \
                           char *gencode)                            \
    {                                                                \
        GEN("cycle ++;\n");                                          \
        code;                                                        \
        if (!insn_is_branch(ir->opcode)) {                           \
            GEN("  PC += %d;\n", insn_len_table[ir->opcode]);        \
        }                                                            \
    }

#include "jit_template.c"
/*
 * In the decoding and emulation stage, specific information is stored in the
 * IR, such as register numbers and immediates. We can leverage this information
 * to generate more efficient code instead of relying on the original source
 * code.
 */
RVOP(jal, {
    UPDATE_PC(ir->pc + ir->imm);
    if (ir->rd) {
        GEN("  rv->X[%u] = %u;\n", ir->rd, ir->pc + 4);
    }
    if (ir->branch_taken) {
        NEXT_INSN(ir->pc + ir->imm);
    } else {
        END_INSN;
    }
})

RVOP(jalr, {
    GEN("  PC = (rv->X[%u] + %d) & ~1U;\n", ir->rs1, ir->imm);
    if (ir->rd) {
        GEN("  rv->X[%u] = %u;\n", ir->rd, ir->pc + 4);
    }
    END_INSN;
})

#define BRANCH_FUNC(type, comp)                                               \
    GEN("  if ((%s) rv->X[%u] %s (%s) rv->X[%u]) {\n", #type, ir->rs1, #comp, \
        #type, ir->rs2);                                                      \
    UPDATE_PC(ir->pc + ir->imm);                                              \
    if (ir->branch_taken) {                                                   \
        NEXT_INSN(ir->pc + ir->imm);                                          \
    } else {                                                                  \
        END_INSN;                                                             \
    }                                                                         \
    GEN("  }\n");                                                             \
    UPDATE_PC(ir->pc + 4);                                                    \
    if (ir->branch_untaken) {                                                 \
        NEXT_INSN(ir->pc + 4);                                                \
    } else {                                                                  \
        END_INSN;                                                             \
    }

RVOP(beq, { BRANCH_FUNC(uint32_t, ==); })

RVOP(bne, { BRANCH_FUNC(uint32_t, !=); })

RVOP(blt, { BRANCH_FUNC(int32_t, <); })

RVOP(bge, { BRANCH_FUNC(int32_t, >=); })

RVOP(bltu, { BRANCH_FUNC(uint32_t, <); })

RVOP(bgeu, { BRANCH_FUNC(uint32_t, >=); })

RVOP(lb, {
    GEN("  addr = rv->X[%u] + %d;\n", ir->rs1, ir->imm);
    GEN("  rv->X[%u] = sign_extend_b(*((const uint8_t *) (m->mem_base + "
        "addr)));\n",
        ir->rd);
})

RVOP(lh, {
    GEN("  addr = rv->X[%u] + %d;\n", ir->rs1, ir->imm);
    GEN("  rv->X[%u] = sign_extend_h(*((const uint16_t *) (m->mem_base + "
        "addr)));\n",
        ir->rd);
})

#define MEMORY_FUNC(type, IO)                                                  \
    GEN("  addr = rv->X[%u] + %d;\n", ir->rs1, ir->imm);                       \
    IIF(IO)                                                                    \
    (GEN("  rv->X[%u] = *((const %s *) (m->mem_base + addr));\n", ir->rd,      \
         #type),                                                               \
     GEN("  *((%s *) (m->mem_base + addr)) = (%s) rv->X[%u];\n", #type, #type, \
         ir->rs2));

RVOP(lw, {MEMORY_FUNC(uint32_t, 1)})

RVOP(lbu, {MEMORY_FUNC(uint8_t, 1)})

RVOP(lhu, {MEMORY_FUNC(uint16_t, 1)})

RVOP(sb, {MEMORY_FUNC(uint8_t, 0)})

RVOP(sh, {MEMORY_FUNC(uint16_t, 0)})

RVOP(sw, {MEMORY_FUNC(uint32_t, 0)})

FORCE_INLINE void gen_shift_func(const rv_insn_t *ir, char *gencode)
{
    switch (ir->opcode) {
    case rv_insn_slli:
        GEN(" rv->X[%u] = rv->X[%u] << %d;", ir->rd, ir->rs1, (ir->imm & 0x1f));
        break;
    case rv_insn_srli:
        GEN(" rv->X[%u] = rv->X[%u] >> %d;", ir->rd, ir->rs1, (ir->imm & 0x1f));
        break;
    case rv_insn_srai:
        GEN(" rv->X[%u] = ((int32_t) rv->X[%u]) >> %d;", ir->rd, ir->rs1,
            (ir->imm & 0x1f));
        break;
    default:
        __UNREACHABLE;
        break;
    }
};

RVOP(slli, { gen_shift_func(ir, gencode); })

RVOP(srli, { gen_shift_func(ir, gencode); })

RVOP(srai, { gen_shift_func(ir, gencode); })

#if RV32_HAS(EXT_F)
RVOP(flw, {
    GEN("  addr = rv->X[%u] + %d;\n", ir->rs1, ir->imm);
    GEN("  rv->F_int[%u] = *((const uint32_t *) (m->mem_base + addr));\n",
        ir->rd);
})

/* FSW */
RVOP(fsw, {
    GEN("  addr = rv->X[%u] + %d;\n", ir->rs1, ir->imm);
    GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->F_int[%u];\n", ir->rs2);
})
#endif

#if RV32_HAS(EXT_C)
RVOP(clw, {
    GEN("  addr = rv->X[%u] + %d;\n", ir->rs1, ir->imm);
    GEN("  rv->X[%u] = *((const uint32_t *) (m->mem_base + addr));\n", ir->rd);
})

RVOP(csw, {
    GEN("  addr = rv->X[%u] + %d;\n", ir->rs1, ir->imm);
    GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->X[%u];\n", ir->rs2);
})

RVOP(cjal, {
    GEN("  rv->X[1] = PC + 2;\n");
    UPDATE_PC(ir->pc + ir->imm);
    if (ir->branch_taken) {
        NEXT_INSN(ir->pc + ir->imm);
    } else {
        END_INSN;
    }
})

RVOP(cj, {
    UPDATE_PC(ir->pc + ir->imm);
    if (ir->branch_taken) {
        NEXT_INSN(ir->pc + ir->imm);
    } else {
        END_INSN;
    }
})

RVOP(cjr, {
    GEN("    rv->csr_cycle = cycle;\n");
    GEN("    rv->PC = rv->X[%d];\n", ir->rs1);
    GEN("    return true;\n");
})

RVOP(cjalr, {
    GEN("    rv->X[rv_reg_ra] = PC + 2;");
    GEN("    rv->csr_cycle = cycle;\n");
    GEN("    rv->PC = rv->X[%u];\n", ir->rs1);
    GEN("    return true;\n");
})

RVOP(cbeqz, {
    GEN("  if (!rv->X[%u]){\n", ir->rs1);
    UPDATE_PC(ir->pc + ir->imm);
    if (ir->branch_taken) {
        NEXT_INSN(ir->pc + ir->imm);
    } else {
        END_INSN;
    }
    GEN("  }\n");
    UPDATE_PC(ir->pc + 2);
    if (ir->branch_untaken) {
        NEXT_INSN(ir->pc + 2);
    } else {
        END_INSN;
    }
})

RVOP(cbnez, {
    GEN("  if (rv->X[%u]){\n", ir->rs1);
    UPDATE_PC(ir->pc + ir->imm);
    if (ir->branch_taken) {
        NEXT_INSN(ir->pc + ir->imm);
    } else {
        END_INSN;
    }
    GEN("  }\n");
    UPDATE_PC(ir->pc + 2);
    if (ir->branch_untaken) {
        NEXT_INSN(ir->pc + 2);
    } else {
        END_INSN;
    }
})

RVOP(clwsp, {
    GEN("addr = rv->X[rv_reg_sp] + %d;\n", ir->imm);
    GEN("  rv->X[%u] = *((const uint32_t *) (m->mem_base + addr));\n", ir->rd);
})

RVOP(cswsp, {
    GEN("addr = rv->X[rv_reg_sp] + %d;\n", ir->imm);
    GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->X[%u];\n", ir->rs2);
})
#endif


static void gen_fuse1(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, char *gencode)
{
    GEN("cycle += %u;\n", ir->imm2);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        GEN("  rv->X[%u] = %d;\n", fuse[i].rd, fuse[i].imm);
    }
}

static void gen_fuse2(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, char *gencode)
{
    GEN("   cycle += 2;\n");
    GEN("  rv->X[%u] = %d;\n", ir->rd, ir->imm);
    GEN("  rv->X[%u] = rv->X[%u] + rv->X[%u];\n", ir->rs2, ir->rd, ir->rs1);
}

static void gen_fuse3(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, char *gencode)
{
    GEN("cycle += %u;\n", ir->imm2);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        GEN("  addr = rv->X[%u] + %d;\n", fuse[i].rs1, fuse[i].imm);
        GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->X[%u];\n",
            fuse[i].rs2)
    }
}

static void gen_fuse4(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, char *gencode)
{
    GEN("cycle += %u;\n", ir->imm2);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        GEN("  addr = rv->X[%u] + %d;\n", fuse[i].rs1, fuse[i].imm);
        GEN("  rv->X[%u] = *((const uint32_t *) (m->mem_base + addr));\n",
            fuse[i].rd);
    }
}

static void gen_fuse5(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, char *gencode)
{
    GEN("   cycle += 2;\n");
    GEN("   rv->io.on_memset(rv);\n");
    GEN("   rv->csr_cycle = cycle;\n");
    GEN("   return true;\n");
}

static void gen_fuse6(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, char *gencode)
{
    GEN("   cycle += 2;\n");
    GEN("   rv->io.on_memcpy(rv);\n");
    GEN("   rv->csr_cycle = cycle;\n");
    GEN("   return true;\n");
}

static void gen_fuse7(riscv_t *rv UNUSED, rv_insn_t *ir UNUSED, char *gencode)
{
    GEN("cycle += %u;\n", ir->imm2);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++)
        gen_shift_func((const rv_insn_t *) (&fuse[i]), gencode);
}
#undef RVOP

static void trace_ebb(riscv_t *rv, char *gencode, rv_insn_t *ir, set_t *set)
{
    if (!set_add(set, ir->pc))
        return;
    GEN("insn_%x:\n", (ir->pc));
    while (1) {
        dispatch_table[ir->opcode](rv, ir, gencode);
        if (!ir->next)
            break;
        ir = ir->next;
    }

    if (ir->branch_untaken && !set_has(set, ir->branch_untaken->pc)) {
        trace_ebb(rv, gencode, ir->branch_untaken, set);
    }

    if (ir->branch_taken && !set_has(set, ir->branch_taken->pc)) {
        trace_ebb(rv, gencode, ir->branch_taken, set);
    }
}

#define EPILOGUE "}"

static void trace_and_gencode(riscv_t *rv, char *gencode)
{
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    dispatch_table[rv_insn_##inst] = &gen_##inst;
    RV_INSN_LIST
#undef _
#define _(inst) dispatch_table[rv_insn_##inst] = &gen_##inst;
    FUSE_INSN_LIST
#undef _
    set_t set;
    strcat(gencode, PROLOGUE);
    set_reset(&set);
    block_t *block = cache_get(rv->block_cache, rv->PC, false);
    trace_ebb(rv, gencode, block->ir_head, &set);
    strcat(gencode, EPILOGUE);
}

#define BINBUF_CAP 64 * 1024
static uint8_t elfbuf[BINBUF_CAP] = {0};
static code_string_t *jit_code_string = NULL;

static uint8_t *compile(riscv_t *rv)
{
    int saved_stdout = dup(STDOUT_FILENO);
    int outp[2];

    if (pipe(outp) != 0)
        printf("cannot make a pipe\n");
    dup2(outp[1], STDOUT_FILENO);
    close(outp[1]);

    FILE *f;
    f = popen("clang -O2 -c -xc -o /dev/stdout -", "w");
    if (f == NULL)
        printf("cannot compile program\n");
    fwrite(jit_code_string->code, 1, jit_code_string->code_size, f);
    pclose(f);
    fflush(stdout);

    assert(read(outp[0], elfbuf, BINBUF_CAP));
    dup2(saved_stdout, STDOUT_FILENO);

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *) elfbuf;

    /**
     * for some instructions, clang will generate a corresponding .rodata
     * section. this means we need to write a mini-linker that puts the .rodata
     * section into memory, takes its actual address, and uses symbols and
     * relocations to apply it back to the corresponding location in the .text
     * section.
     */

    long long text_idx = 0, symtab_idx = 0, rela_idx = 0, rodata_idx = 0;
    {
        uint64_t shstr_shoff =
            ehdr->e_shoff + ehdr->e_shstrndx * sizeof(elf64_shdr_t);
        elf64_shdr_t *shstr_shdr = (elf64_shdr_t *) (elfbuf + shstr_shoff);
        assert(ehdr->e_shnum != 0);

        for (long long idx = 0; idx < ehdr->e_shnum; idx++) {
            uint64_t shoff = ehdr->e_shoff + idx * sizeof(elf64_shdr_t);
            elf64_shdr_t *shdr = (elf64_shdr_t *) (elfbuf + shoff);
            char *str =
                (char *) (elfbuf + shstr_shdr->sh_offset + shdr->sh_name);
            if (strcmp(str, ".text") == 0)
                text_idx = idx;
            if (strcmp(str, ".rela.text") == 0)
                rela_idx = idx;
            if (strncmp(str, ".rodata.", strlen(".rodata.")) == 0)
                rodata_idx = idx;
            if (strcmp(str, ".symtab") == 0)
                symtab_idx = idx;
        }
    }

    assert(text_idx != 0 && symtab_idx != 0);

    uint64_t text_shoff = ehdr->e_shoff + text_idx * sizeof(elf64_shdr_t);
    elf64_shdr_t *text_shdr = (elf64_shdr_t *) (elfbuf + text_shoff);
    if (rela_idx == 0 || rodata_idx == 0)
        return code_cache_add(rv->block_cache, rv->PC,
                              elfbuf + text_shdr->sh_offset, text_shdr->sh_size,
                              text_shdr->sh_addralign);

    uint64_t shoff = ehdr->e_shoff + rodata_idx * sizeof(elf64_shdr_t);
    elf64_shdr_t *shdr = (elf64_shdr_t *) (elfbuf + shoff);
    code_cache_add(rv->block_cache, rv->PC, elfbuf + shdr->sh_offset,
                   shdr->sh_size, shdr->sh_addralign);
    uint64_t text_addr = (uint64_t) code_cache_add(
        rv->block_cache, rv->PC, elfbuf + text_shdr->sh_offset,
        text_shdr->sh_size, text_shdr->sh_addralign);

    // apply relocations to .text section.
    {
        uint64_t shoff = ehdr->e_shoff + rela_idx * sizeof(elf64_shdr_t);
        elf64_shdr_t *shdr = (elf64_shdr_t *) (elfbuf + shoff);
        long long rels = shdr->sh_size / sizeof(elf64_rela_t);

        uint64_t symtab_shoff =
            ehdr->e_shoff + symtab_idx * sizeof(elf64_shdr_t);
        elf64_shdr_t *symtab_shdr = (elf64_shdr_t *) (elfbuf + symtab_shoff);

        for (long long idx = 0; idx < rels; idx++) {
#ifndef __x86_64__
            fatal("only support x86_64 for now");
#endif
            elf64_rela_t *rel = (elf64_rela_t *) (elfbuf + shdr->sh_offset +
                                                  idx * sizeof(elf64_rela_t));
            assert(rel->r_type == R_X86_64_PC32);

            elf64_sym_t *sym =
                (elf64_sym_t *) (elfbuf + symtab_shdr->sh_offset +
                                 rel->r_sym * sizeof(elf64_sym_t));
            uint32_t *loc = (uint32_t *) (text_addr + rel->r_offset);
            *loc = (uint32_t) ((long long) sym->st_value + rel->r_addend -
                               (long long) rel->r_offset);
        }
    }

    return (uint8_t *) text_addr;
}

uint8_t *block_compile(riscv_t *rv)
{
    if (!jit_code_string) {
        jit_code_string = malloc(sizeof(code_string_t));
        jit_code_string->code = calloc(1, 1024 * 1024);
    } else {
        memset(jit_code_string->code, 0, 1024 * 1024);
    }
    jit_code_string->curr = 0;
    trace_and_gencode(rv, jit_code_string->code);
    jit_code_string->code_size = strlen(jit_code_string->code);
    // char *bufff = malloc(30);
    // sprintf(bufff, "%#x.c", rv->PC);
    // FILE *fp = fopen(bufff, "w+");
    // fwrite(jit_code_string->code, 1, jit_code_string->code_size, fp);
    // fclose(fp);
    return compile(rv);
}
