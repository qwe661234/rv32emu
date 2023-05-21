/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "codegen.h"
#include "decode.h"
#include "softfloat.h"

#define SET_SIZE 1024

typedef struct {
    uint32_t table[SET_SIZE][32];
} set_t;

static inline uint32_t hash(uint32_t key)
{
    return (key >> 1) & (SET_SIZE - 1);
}

void set_reset(set_t *set)
{
    memset(set, 0, sizeof(set_t));
}

bool set_add(set_t *set, uint32_t key)
{
    uint32_t index = hash(key);
    uint8_t count = 0;
    while (set->table[index][count] != 0) {
        if (set->table[index][count++] == key)
            return false;
    }

    set->table[index][count] = key;
    return true;
}

bool set_has(set_t *set, uint32_t key)
{
    uint32_t index = hash(key);
    uint8_t count = 0;
    while (set->table[index][count] != 0) {
        if (set->table[index][count++] == key)
            return true;
    }
    return false;
}

static set_t set;
static bool insn_is_branch(uint8_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch) IIF(can_branch)(case rv_insn_##inst:, )
        RISCV_INSN_LIST
#undef _
        return true;
    }
    return false;
}
typedef void (*gen_func_t)(riscv_t *, const rv_insn_t *, char *, uint32_t);
static gen_func_t dispatch_table[rv_insn_qty];
static char funcbuf[128] = {0};
#define RVOP(inst, code)                                                  \
    static void gen_##inst(riscv_t *rv UNUSED, const rv_insn_t *ir,       \
                           char *gencode, uint32_t pc)                    \
    {                                                                     \
        sprintf(funcbuf, "insn_%x:\n", (pc));                             \
        strcat(gencode, funcbuf);                                         \
        strcat(gencode, "    rv->X[0] = 0;\n");                           \
        strcat(gencode, "    rv->csr_cycle++;\n");                        \
        code;                                                             \
        if (!insn_is_branch(ir->opcode)) {                                \
            sprintf(funcbuf, "    rv->PC += %d;\n", (ir->insn_len));      \
            strcat(gencode, funcbuf);                                     \
            sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->insn_len)); \
            strcat(gencode, funcbuf);                                     \
        }                                                                 \
    }

RVOP(nop, {/* no operation */});

RVOP(lui, {
    sprintf(funcbuf, "    rv->X[%u] = %u;\n", ir->rd, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(auipc, {
    sprintf(funcbuf, "    rv->X[%u] = %u + rv->PC;\n", ir->rd, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(jal, {
    strcat(gencode, "    pc = rv->PC;\n");
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->imm);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = pc + %u;\n", ir->rd, ir->insn_len);
        strcat(gencode, funcbuf);
    }
    sprintf(funcbuf, "        goto insn_%x;\n", (pc + ir->imm));
    strcat(gencode, funcbuf);
})

RVOP(jalr, {
    strcat(gencode, "    pc = rv->PC;\n");
    sprintf(funcbuf, "    rv->PC = (rv->X[%u] + %u) & ~1U;\n", ir->rs1,
            ir->imm);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = pc + %u;\n", ir->rd, ir->insn_len);
        strcat(gencode, funcbuf);
    }
    strcat(gencode, "    return true;\n");
})

#define BRNACH_FUNC(type, comp)                                              \
    sprintf(funcbuf, "    if ((%s) rv->X[%u] %s (%s) rv->X[%u]) {\n", #type, \
            ir->rs1, #comp, #type, ir->rs2);                                 \
    strcat(gencode, funcbuf);                                                \
    if (ir->branch_taken) {                                                  \
        sprintf(funcbuf, "        rv->PC += %u;\n", ir->imm);                \
        strcat(gencode, funcbuf);                                            \
        sprintf(funcbuf, "        goto insn_%x;\n", (pc + ir->imm));         \
        strcat(gencode, funcbuf);                                            \
    } else {                                                                 \
        sprintf(funcbuf, "        rv->PC += %u;\n", ir->imm);                \
        strcat(gencode, funcbuf);                                            \
        strcat(gencode, "        return true;\n");                           \
    }                                                                        \
    strcat(gencode, "   }\n");                                               \
    if (ir->branch_untaken) {                                                \
        sprintf(funcbuf, "    rv->PC += %u;\n", ir->insn_len);               \
        strcat(gencode, funcbuf);                                            \
        sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->insn_len));        \
        strcat(gencode, funcbuf);                                            \
    } else {                                                                 \
        sprintf(funcbuf, "        rv->PC += %u;\n", ir->insn_len);           \
        strcat(gencode, funcbuf);                                            \
        strcat(gencode, "        return true;\n");                           \
    }

RVOP(beq, { BRNACH_FUNC(uint32_t, ==); })

RVOP(bne, { BRNACH_FUNC(uint32_t, !=); })

RVOP(blt, { BRNACH_FUNC(int32_t, <); })

RVOP(bge, { BRNACH_FUNC(int32_t, >=); })

RVOP(bltu, { BRNACH_FUNC(uint32_t, <); })

RVOP(bgeu, { BRNACH_FUNC(uint32_t, >=); })

RVOP(lb, {
    sprintf(funcbuf, "    addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "    c = m->chunks[addr >> 16];\n");
    sprintf(funcbuf,
            "    rv->X[%u] = sign_extend_b(* (const uint32_t *) (c->data + "
            "(addr & 0xffff)));\n",
            ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(lh, {
    sprintf(funcbuf, "    addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "    c = m->chunks[addr >> 16];\n");
    sprintf(funcbuf,
            "    rv->X[%u] = sign_extend_h(* (const uint32_t *) (c->data + "
            "(addr & 0xffff)));\n",
            ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(lw, {
    sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "   c = m->chunks[addr >> 16];\n");
    sprintf(
        funcbuf,
        "   rv->X[%u] = * (const uint32_t *) (c->data + (addr & 0xffff));\n",
        ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(lbu, {
    sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "   c = m->chunks[addr >> 16];\n");
    sprintf(funcbuf,
            "   rv->X[%u] = * (const uint8_t *) (c->data + (addr & 0xffff));\n",
            ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(lhu, {
    sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "   c = m->chunks[addr >> 16];\n");
    sprintf(funcbuf,
            "   rv->X[%u] = * (const uint16_t *) (c->data + "
            "(addr & 0xffff));\n",
            ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(sb, {
    sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "   c = m->chunks[addr >> 16];\n");
    sprintf(funcbuf,
            "*(uint8_t *) (c->data + (addr & 0xffff)) = (uint8_t) rv->X[%u];\n",
            ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(sh, {
    sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "   c = m->chunks[addr >> 16];\n");
    sprintf(
        funcbuf,
        "*(uint16_t *) (c->data + (addr & 0xffff)) = (uint16_t) rv->X[%u];\n",
        ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(sw, {
    sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    strcat(gencode, "   c = m->chunks[addr >> 16];\n");
    sprintf(funcbuf, "*(uint32_t *) (c->data + (addr & 0xffff)) = rv->X[%u];\n",
            ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(addi, {
    sprintf(funcbuf, "   rv->X[%u] = (int32_t) (rv->X[%u]) + %u;\n", ir->rd,
            ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(slti, {
    sprintf(funcbuf, "   rv->X[%u] = ((int32_t) (rv->X[%u]) < %u) ? 1 : 0;\n",
            ir->rd, ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(sltiu, {
    sprintf(funcbuf, "   rv->X[%u] = (rv->X[%u] < (uint32_t) %u) ? 1 : 0;\n",
            ir->rd, ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(xori, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] ^ %u;\n", ir->rd, ir->rs1,
            ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(ori, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] | %u;\n", ir->rd, ir->rs1,
            ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(andi, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] & %u;\n", ir->rd, ir->rs1,
            ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(slli, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] << %u;\n", ir->rd, ir->rs1,
            ir->imm & 0x1f);
    strcat(gencode, funcbuf);
})

RVOP(srli, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] >> %u;\n", ir->rd, ir->rs1,
            ir->imm & 0x1f);
    strcat(gencode, funcbuf);
})

RVOP(srai, {
    sprintf(funcbuf, "   rv->X[%u] = ((int32_t) rv->X[%u]) >> %u;\n", ir->rd,
            ir->rs1, ir->imm & 0x1f);
    strcat(gencode, funcbuf);
})

RVOP(add, {
    sprintf(funcbuf,
            "   rv->X[%u] = (int32_t) (rv->X[%u]) + (int32_t) (rv->X[%u]);\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(sub, {
    sprintf(funcbuf,
            "   rv->X[%u] = (int32_t) (rv->X[%u]) - (int32_t) (rv->X[%u]);\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(sll, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] << (rv->X[%u] & 0x1f);\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(slt, {
    sprintf(funcbuf,
            "   rv->X[%u] = ((int32_t) (rv->X[%u]) < (int32_t) (rv->X[%u])) ? "
            "1 : 0;\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(sltu, {
    sprintf(funcbuf, "   rv->X[%u] = ((rv->X[%u]) < (rv->X[%u])) ? 1 : 0;\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(xor, {
  sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] ^ rv->X[%u];\n", ir->rd, ir->rs1,
          ir->rs2);
  strcat(gencode, funcbuf);
})

RVOP(srl, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] >> (rv->X[%u] & 0x1f);\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(sra, {
    sprintf(funcbuf,
            "   rv->X[%u] = ((int32_t) rv->X[%u]) >> (rv->X[%u] & 0x1f);\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(or, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] | rv->X[%u];\n", ir->rd, ir->rs1,
            ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(and, {
    sprintf(funcbuf, "   rv->X[%u] = rv->X[%u] & rv->X[%u];\n", ir->rd, ir->rs1,
            ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(ecall, {
    strcat(gencode, "   rv->compressed = false;\n");
    strcat(gencode, "   rv->io.on_ecall((riscv_t *)rv);\n");
    strcat(gencode, "   return true;\n");
})

RVOP(ebreak, {
    strcat(gencode, "   rv->compressed = false;\n");
    strcat(gencode, "   rv->io.on_ebreak((riscv_t *)rv);\n");
    strcat(gencode, "   return true;\n");
})

RVOP(wfi, { strcat(gencode, "   return false;\n"); })

RVOP(uret, { strcat(gencode, "   return false;\n"); })

RVOP(sret, { strcat(gencode, "   return false;\n"); })

RVOP(hret, { strcat(gencode, "   return false;\n"); })

RVOP(mret, {
    sprintf(funcbuf, "    rrv->PC = rv->csr_mepc;\n");
    strcat(gencode, funcbuf);
    strcat(gencode, "   return true;\n");
})

#if RV32_HAS(Zifencei) /* RV32 Zifencei Standard Extension */
RVOP(fencei, {
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->insn_len);
    strcat(gencode, funcbuf);
    strcat(gencode, "   return true;\n");
})
#endif

#if RV32_HAS(Zicsr) /* RV32 Zicsr Standard Extension */
RVOP(csrrw, {
    sprintf(funcbuf, "    tmp = csr_csrrw(rv, %u, rv->X[%u]);\n", ir->imm,
            ir->rs1);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = tmp;\n", ir->rd);
        strcat(gencode, funcbuf);
    }
})

RVOP(csrrs, {
    if (!ir->rs1)
        sprintf(funcbuf, "    tmp = csr_csrrs(rv, %u, 0U);\n", ir->imm);
    else
        sprintf(funcbuf, "    tmp = csr_csrrs(rv, %u, rv->X[%u]);\n", ir->imm,
                ir->rs1);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = tmp;\n", ir->rd);
        strcat(gencode, funcbuf);
    }
})

RVOP(csrrc, {
    if (!ir->rs1)
        sprintf(funcbuf, "    tmp = csr_csrrc(rv, %u, ~0U);\n", ir->imm);
    else
        sprintf(funcbuf, "    tmp = csr_csrrc(rv, %u, rv->X[%u]);\n", ir->imm,
                ir->rs1);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = tmp;\n", ir->rd);
        strcat(gencode, funcbuf);
    }
})

RVOP(csrrwi, {
    sprintf(funcbuf, "    tmp = csr_csrrw(rv, %u, %u);\n", ir->imm, ir->rs1);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = tmp;\n", ir->rd);
        strcat(gencode, funcbuf);
    }
})

RVOP(csrrsi, {
    sprintf(funcbuf, "    tmp = csr_csrrs(rv, %u, %u);\n", ir->imm, ir->rs1);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = tmp;\n", ir->rd);
        strcat(gencode, funcbuf);
    }
})

RVOP(csrrci, {
    sprintf(funcbuf, "    tmp = csr_csrrc(rv, %u, %u);\n", ir->imm, ir->rs1);
    strcat(gencode, funcbuf);
    if (ir->rd) {
        sprintf(funcbuf, "    rv->X[%u] = tmp;\n", ir->rd);
        strcat(gencode, funcbuf);
    }
})
#endif

#if RV32_HAS(EXT_M) /* RV32M Standard Extension */
RVOP(mul, {
    sprintf(funcbuf,
            "   rv->X[%u] = (int32_t) rv->X[%u] * (int32_t) rv->X[%u];\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(mulh, {
    sprintf(funcbuf, "   multiplicand = (int32_t) rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "   multiplier = (int32_t) rv->X[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "   rv->X[%u] = ((uint64_t) (multiplicand * multiplier)) >> 32;\n",
            ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(mulhsu, {
    sprintf(funcbuf, "    multiplicand = (int32_t) rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    umultiplier = rv->X[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(
        funcbuf,
        "    rv->X[%u] = ((uint64_t) (multiplicand * umultiplier)) >> 32;\n",
        ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(mulhu, {
    sprintf(funcbuf,
            "    rv->X[%u] =  ((uint64_t) rv->X[%u] * (uint64_t) rv->X[%u]) >> "
            "32;\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(div, {
    sprintf(funcbuf, "    dividend = (int32_t) rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    divisor = (int32_t) rv->X[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->X[%u] = !divisor ? ~0U : (divisor == -1 && ",
            ir->rd);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "rv->X[%u] == 0x80000000U) ? rv->X[%u] : (unsigned int) (dividend "
            "/ divisor);\n",
            ir->rs1, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(divu, {
    sprintf(funcbuf, "    udividend = rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    udivisor = rv->X[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    rv->X[%u] = !udivisor ? ~0U : udividend / udivisor;\n",
            ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(rem, {
    sprintf(funcbuf, "    dividend = (int32_t) rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    divisor = (int32_t) rv->X[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    rv->X[%u] = !divisor ? dividend : (divisor == -1 && rv->X[%u] "
            "== 0x80000000U) ? 0 : (dividend %% divisor);\n",
            ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(remu, {
    sprintf(funcbuf, "    udividend = rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    udivisor = rv->X[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    rv->X[%u] = !udivisor ? udividend : udividend %% udivisor;\n",
            ir->rd);
    strcat(gencode, funcbuf);
})
#endif

#if RV32_HAS(EXT_A)

RVOP(lrw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, rv->X[%u]);\n",
            ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(scw, {
    sprintf(funcbuf, "    rv->io.mem_write_w(rv, rv->X[%u], rv->X[%u]);\n",
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->X[%u] = 0;\n", ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(amoswapw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, rv->X[%u]);\n", ir->rs1,
            ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(amoaddw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    res = (int32_t) rv->X[%u] + (int32_t) rv->X[%u];\n",
            ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(amoxorw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    res = rv->X[%u] ^ rv->X[%u];\n", ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(amoandw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    res = rv->X[%u] & rv->X[%u];\n", ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(amoorw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    res = rv->X[%u] | rv->X[%u];\n", ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(amominw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    res = (int32_t) rv->X[%u] < (int32_t) rv->X[%u] ? rv->X[%u] : "
            "rv->X[%u];\n",
            ir->rd, ir->rs2, ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(amomaxw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    res = (int32_t) rv->X[%u] > (int32_t) rv->X[%u] ? rv->X[%u] : "
            "rv->X[%u];\n",
            ir->rd, ir->rs2, ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(amominuw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    res = rv->X[%u] < rv->X[%u] ? rv->X[%u] : rv->X[%u];\n",
            ir->rd, ir->rs2, ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(amomaxuw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    res = rv->X[%u] > rv->X[%u] ? rv->X[%u] : rv->X[%u];\n",
            ir->rd, ir->rs2, ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
    strcat(gencode, funcbuf);
})
#endif /* RV32_HAS(EXT_A) */

#if RV32_HAS(EXT_F) /* RV32F Standard Extension */
RVOP(flw, {
    sprintf(funcbuf, "    data = rv->io.mem_read_w(rv, rv->X[%u] + %u);\n",
            ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->F_int[%u] = data;\n", ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(fsw, {
    sprintf(funcbuf, "    data = rv->F_int[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_w(rv, rv->X[%u] + %u, data);\n",
            ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(fmadds, {
    sprintf(funcbuf, "    rv->F[%u] = rv->F[%u] * rv->F[%u] + rv->F[%u];\n",
            ir->rd, ir->rs1, ir->rs2, ir->rs3);
    strcat(gencode, funcbuf);
})

RVOP(fmsubs, {
    sprintf(funcbuf, "    rv->F[%u] = rv->F[%u] * rv->F[%u] - rv->F[%u];\n",
            ir->rd, ir->rs1, ir->rs2, ir->rs3);
    strcat(gencode, funcbuf);
})

RVOP(fnmsubs, {
    sprintf(funcbuf, "    rv->F[%u] = rv->F[%u] - (rv->F[%u] * rv->F[%u]);\n",
            ir->rd, ir->rs3, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(fnmadds, {
    sprintf(funcbuf, "    rv->F[%u] = -(rv->F[%u] * rv->F[%u]) - rv->F[%u];\n",
            ir->rd, ir->rs1, ir->rs2, ir->rs3);
    strcat(gencode, funcbuf);
})

RVOP(fadds, {
    sprintf(funcbuf,
            "    if (isnanf(rv->F[%u]) || isnanf(rv->F[%u]) || "
            "isnanf(rv->F[%u] + rv->F[%u])) {\n",
            ir->rs1, ir->rs2, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->F_int[%u] = %u;\n", ir->rd, RV_NAN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->csr_fcsr |= %u;\n", FFLAG_INVALID_OP);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    } else {\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->F[%u] = rv->F[%u] + rv->F[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    }\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    if (isinff(rv->F[%d])) {\n", ir->rd);
    sprintf(funcbuf, "        rv->csr_fcsr |= %u;\n", FFLAG_OVERFLOW);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->csr_fcsr |= %u;\n", FFLAG_INEXACT);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    }\n");
})

/* FSUB.S */
RVOP(fsubs, {
    sprintf(funcbuf, "    if (isnanf(rv->F[%u]) || isnanf(rv->F[%u]))\n",
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->F_int[%u] = %u;\n", ir->rd, RV_NAN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    else\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->F[%u] = rv->F[%u] - rv->F[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(fmuls, {
    sprintf(funcbuf, "    rv->F[%u] = rv->F[%u] * rv->F[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(fdivs, {
    sprintf(funcbuf, "    rv->F[%u] = rv->F[%u] / rv->F[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(fsqrts, {
    sprintf(funcbuf, "    rv->F[%u] = sqrtf(rv->F[%u]);\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(fsgnjs, {
    sprintf(funcbuf,
            "    ures = ((uint32_t)rv->F_int[%u] & %u) | "
            "((uint32_t)rv->F_int[%u] & %u);\n",
            ir->rs1, ~FMASK_SIGN, ir->rs2, FMASK_SIGN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->F_int[%d] = ures;\n", ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(fsgnjns, {
    sprintf(funcbuf,
            "    ures = ((uint32_t)rv->F_int[%u] & %u) | "
            "(~(uint32_t)rv->F_int[%u] & %u);\n",
            ir->rs1, ~FMASK_SIGN, ir->rs2, FMASK_SIGN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->F_int[%d] = ures;\n", ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(fsgnjxs, {
    sprintf(funcbuf,
            "    ures = (uint32_t)rv->F_int[%u] | "
            "(~(uint32_t)rv->F_int[%u] & %u);\n",
            ir->rs1, ir->rs2, FMASK_SIGN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->F_int[%d] = ures;\n", ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(fmins, {
    sprintf(funcbuf, "    a = rv->F_int[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    b = rv->F_int[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    if (is_nan(a) || is_nan(b)) {\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        if (is_snan(a) || is_snan(b))\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->csr_fcsr |= %u;\n", FFLAG_INVALID_OP);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        if (is_nan(a) && !is_nan(b))\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        else if (!is_nan(a) && is_nan(b))\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        else\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->F_int[%u] = %u;\n", ir->rd, RV_NAN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    } else {\n");
    sprintf(funcbuf, "            a &= %u;\n", FMASK_SIGN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            b &= %u;\n", FMASK_SIGN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            if (a_sign != b_sign)\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "                rv->F[%u] = a_sign ? rv->F[%u] : rv->F[%u];\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            else\n");
    sprintf(funcbuf,
            "                 rv->F[%u] = (rv->F[%u] < rv->F[%u]) ? rv->F[%u] "
            ": rv->F[%u];\n",
            ir->rd, ir->rs1, ir->rs2, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    }\n");
})

RVOP(fmaxs, {
    sprintf(funcbuf, "    a = rv->F_int[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    b = rv->F_int[%u];\n", ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    if (is_nan(a) || is_nan(b)) {\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        if (is_snan(a) || is_snan(b))\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->csr_fcsr |= %u;\n", FFLAG_INVALID_OP);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        if (is_nan(a) && !is_nan(b))\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        else if (!is_nan(a) && is_nan(b))\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        else\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            rv->F_int[%u] = %u;\n", ir->rd, RV_NAN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    } else {\n");
    sprintf(funcbuf, "            a &= %u;\n", FMASK_SIGN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            b &= %u;\n", FMASK_SIGN);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            if (a_sign != b_sign)\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "                rv->F[%u] = a_sign ? rv->F[%u] : rv->F[%u];\n",
            ir->rd, ir->rs2, ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "            else\n");
    sprintf(funcbuf,
            "                 rv->F[%u] = (rv->F[%u] > rv->F[%u]) ? rv->F[%u] "
            ": rv->F[%u];\n",
            ir->rd, ir->rs1, ir->rs2, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    }\n");
})

RVOP(fcvtws, {
    sprintf(funcbuf, "    rv->X[%u] = (int32_t) rv->F[%u];\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(fcvtwus, {
    sprintf(funcbuf, "    rv->X[%u] = (uint32_t) rv->F[%u];\n", ir->rd,
            ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(fmvxw, {
    sprintf(funcbuf, "    rv->X[%u] = rv->F_int[%u];\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(feqs, {
    sprintf(funcbuf, "    rv->X[%u] = (rv->F[%u] == rv->F[%u]) ? 1 : 0;\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    if (is_nan(rv->F_int[%u]) || is_nan(rv->F_int[%u]))\n",
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->csr_fcsr |= %u;\n", FFLAG_INVALID_OP);
    strcat(gencode, funcbuf);
})

RVOP(flts, {
    sprintf(funcbuf, "    rv->X[%u] = (rv->F[%u] < rv->F[%u]) ? 1 : 0;\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    if (is_nan(rv->F_int[%u]) || is_nan(rv->F_int[%u]))\n",
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->csr_fcsr |= %u;\n", FFLAG_INVALID_OP);
    strcat(gencode, funcbuf);
})

RVOP(fles, {
    sprintf(funcbuf, "    rv->X[%u] = (rv->F[%u] <= rv->F[%u]) ? 1 : 0;\n",
            ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf,
            "    if (is_nan(rv->F_int[%u]) || is_nan(rv->F_int[%u]))\n",
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->csr_fcsr |= %u;\n", FFLAG_INVALID_OP);
    strcat(gencode, funcbuf);
})

RVOP(fclasss, {
    sprintf(funcbuf, "    rv->X[%u] = calc_fclass((uint32_t)rv->F_int[%u])\n",
            ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(fcvtsw, {
    sprintf(funcbuf, "    rv->F[%u] = (int32_t) rv->X[%u];\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(fcvtswu, {
    sprintf(funcbuf, "    rv->F[%u] = rv->X[%u];\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})

RVOP(fmvwx, {
    sprintf(funcbuf, "    rv->F_int[%u] = rv->X[%u];\n", ir->rd, ir->rs1);
    strcat(gencode, funcbuf);
})
#endif

#if RV32_HAS(EXT_C) /* RV32C Standard Extension */
RVOP(caddi4spn, {
    sprintf(funcbuf, "    rv->X[%u] = rv->X[2] + %u;\n", ir->rd,
            (uint16_t) ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(clw, {
    sprintf(funcbuf, "    addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, addr);\n", ir->rd);
    strcat(gencode, funcbuf);
})

RVOP(csw, {
    sprintf(funcbuf, "    addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_w(rv, addr, rv->X[%u]);\n", ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(cnop, {/* no operation */})

RVOP(caddi, {
    sprintf(funcbuf, "    rv->X[%u] += %u;\n", ir->rd, (int16_t) ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(cjal, {
    sprintf(funcbuf, "    rv->X[1] = rv->PC += %u;\n", ir->insn_len);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->imm));
    strcat(gencode, funcbuf);
})

RVOP(cli, {
    sprintf(funcbuf, "    rv->X[%u] = %u;\n", ir->rd, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(caddi16sp, {
    sprintf(funcbuf, "    rv->X[%u] += %u;\n", ir->rd, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(clui, {
    sprintf(funcbuf, "    rv->X[%u] = %u;\n", ir->rd, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(csrli, {
    sprintf(funcbuf, "    rv->X[%u] >>= %u;\n", ir->rs1, ir->shamt);
    strcat(gencode, funcbuf);
})

RVOP(csrai, {
    sprintf(funcbuf, "    mask = 0x80000000 & rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->X[%u] >>= %u;\n", ir->rs1, ir->shamt);
    strcat(gencode, funcbuf);
    for (unsigned int i = 0; i < ir->shamt; ++i) {
        sprintf(funcbuf, "    rv->X[%u] |= mask >> %u;\n", ir->rs1, i);
        strcat(gencode, funcbuf);
    }
})

RVOP(candi, {
    sprintf(funcbuf, "    rv->X[%u] &= %u;\n", ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(csub, {
    sprintf(funcbuf, "    rv->X[%u] = rv->X[%u] - rv->X[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(cxor, {
    sprintf(funcbuf, "    rv->X[%u] = rv->X[%u] ^ rv->X[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(cor, {
    sprintf(funcbuf, "    rv->X[%u] = rv->X[%u] | rv->X[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(cand, {
    sprintf(funcbuf, "    rv->X[%u] = rv->X[%u] & rv->X[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(cj, {
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->imm));
    strcat(gencode, funcbuf);
})

RVOP(cbeqz, {
    sprintf(funcbuf, "    if (!rv->X[%u]){\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->PC += %u;\n", ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        goto insn_%x;\n", (pc + ir->imm));
    strcat(gencode, funcbuf);
    strcat(gencode, "    }\n");
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->insn_len);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->insn_len));
    strcat(gencode, funcbuf);
})

RVOP(cbnez, {
    sprintf(funcbuf, "    if (rv->X[%u]){\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        rv->PC += %u;\n", ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "        goto insn_%x;\n", (pc + ir->imm));
    strcat(gencode, funcbuf);
    strcat(gencode, "    }\n");
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->insn_len);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->insn_len));
    strcat(gencode, funcbuf);
})

RVOP(cslli, {
    sprintf(funcbuf, "    rv->X[%u] <<= %u;\n", ir->rd, (uint8_t) ir->imm);
})

RVOP(clwsp, {
    sprintf(funcbuf, "    addr = rv->X[2] + %u;\n", ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->X[%u] = rv->io.mem_read_w(rv, addr);\n", ir->rd);
})

RVOP(cjr, {
    sprintf(funcbuf, "    rv->PC = rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    return true;\n");
})

RVOP(cmv, {
    sprintf(funcbuf, "    rv->X[%u] = rv->X[%u];\n", ir->rd, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(cebreak, {
    sprintf(funcbuf, "    rv->compressed = true;\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.on_ebreak(rv);\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    return true;\n");
    strcat(gencode, funcbuf);
})

RVOP(cjalr, {
    sprintf(funcbuf, "    jump_to = rv->X[%u];\n", ir->rs1);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->X[1] = rv->PC + %u;\n", ir->insn_len);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->PC = jump_to;\n");
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    return true;\n");
    strcat(gencode, funcbuf);
})

RVOP(cadd, {
    sprintf(funcbuf, "    rv->X[%u] = rv->X[%u] + rv->X[%u];\n", ir->rd,
            ir->rs1, ir->rs2);
    strcat(gencode, funcbuf);
})

RVOP(cswsp, {
    sprintf(funcbuf, "    addr = rv->X[2] + %u;\n", ir->imm);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    rv->io.mem_write_w(rv, addr, rv->X[%u]);\n", ir->rs2);
    strcat(gencode, funcbuf);
})
#endif

RVOP(fuse1, {
    sprintf(funcbuf, "    rv->X[%u] = (int32_t) (rv->PC + %u + %u);\n", ir->rd,
            ir->imm, ir->imm2);
    strcat(gencode, funcbuf);
})

RVOP(fuse2, {
    sprintf(
        funcbuf,
        "    rv->X[%u] = (int32_t) (rv->X[%u]) + (int32_t) (rv->PC + %u);\n",
        ir->rd, ir->rs1, ir->imm);
    strcat(gencode, funcbuf);
})

RVOP(fuse3, {
    mem_fuse_t *mem_fuse = ir->mem_fuse;
    for (int i = 0; i < ir->imm2; i++) {
        sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", mem_fuse[i].rs1,
                mem_fuse[i].imm);
        strcat(gencode, funcbuf);
        strcat(gencode, "   c = m->chunks[addr >> 16];\n");
        sprintf(funcbuf,
                "*(uint32_t *) (c->data + (addr & 0xffff)) = rv->X[%u];\n",
                mem_fuse[i].rs2);
        strcat(gencode, funcbuf);
    }
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->imm2 * ir->insn_len);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->imm2 * ir->insn_len));
    strcat(gencode, funcbuf);
    return;
})

RVOP(fuse4, {
    mem_fuse_t *mem_fuse = ir->mem_fuse;
    for (int i = 0; i < ir->imm2; i++) {
        sprintf(funcbuf, "   addr = rv->X[%u] + %u;\n", mem_fuse[i].rs1,
                mem_fuse[i].imm);
        strcat(gencode, funcbuf);
        strcat(gencode, "   c = m->chunks[addr >> 16];\n");
        sprintf(funcbuf,
                "   rv->X[%u] = * (const uint32_t *) (c->data + (addr & "
                "0xffff));\n",
                mem_fuse[i].rd);
        strcat(gencode, funcbuf);
    }
    sprintf(funcbuf, "    rv->PC += %u;\n", ir->imm2 * ir->insn_len);
    strcat(gencode, funcbuf);
    sprintf(funcbuf, "    goto insn_%x;\n", (pc + ir->imm2 * ir->insn_len));
    strcat(gencode, funcbuf);
    return;
})

RVOP(empty, {})

void trace_ebb(riscv_t *rv, char *gencode, rv_insn_t *ir)
{
    while (1) {
        if (set_add(&set, ir->pc))
            dispatch_table[ir->opcode](rv, ir, gencode, ir->pc);

        if (ir->tailcall)
            break;
        ir++;
    }
    if (ir->branch_untaken && !set_has(&set, ir->branch_untaken->pc))
        trace_ebb(rv, gencode, ir->branch_untaken);
    if (ir->branch_taken && !set_has(&set, ir->branch_taken->pc))
        trace_ebb(rv, gencode, ir->branch_taken);
}

#define PROLOGUE                                                          \
    "#include <stdint.h>\n"                                               \
    "#include <stdbool.h>\n"                                              \
    "typedef struct riscv_internal riscv_t;\n"                            \
    "typedef void *riscv_user_t;\n"                                       \
    "typedef uint32_t riscv_word_t;\n"                                    \
    "typedef uint16_t riscv_half_t;\n"                                    \
    "typedef uint8_t riscv_byte_t;\n"                                     \
    "typedef uint32_t riscv_exception_t;\n"                               \
    "typedef float riscv_float_t;\n"                                      \
    "typedef riscv_word_t (*riscv_mem_ifetch)(riscv_t *rv, riscv_word_t " \
    "addr);\n"                                                            \
    "typedef riscv_word_t (*riscv_mem_read_w)(riscv_t *rv, riscv_word_t " \
    "addr);\n"                                                            \
    "typedef riscv_half_t (*riscv_mem_read_s)(riscv_t *rv, riscv_word_t " \
    "addr);\n"                                                            \
    "typedef riscv_byte_t (*riscv_mem_read_b)(riscv_t *rv, riscv_word_t " \
    "addr);\n"                                                            \
    "typedef void (*riscv_mem_write_w)(riscv_t *rv, riscv_word_t addr, "  \
    "riscv_word_t data);\n"                                               \
    "typedef void (*riscv_mem_write_s)(riscv_t *rv, riscv_word_t addr, "  \
    "riscv_half_t data);\n"                                               \
    "typedef void (*riscv_mem_write_b)(riscv_t *rv, riscv_word_t addr, "  \
    "riscv_half_t data);\n"                                               \
    "typedef void (*riscv_on_ecall)(riscv_t *rv);\n"                      \
    "typedef void (*riscv_on_ebreak)(riscv_t *rv);\n"                     \
    "typedef struct {\n"                                                  \
    "    riscv_mem_ifetch mem_ifetch;\n"                                  \
    "    riscv_mem_read_w mem_read_w;\n"                                  \
    "    riscv_mem_read_s mem_read_s;\n"                                  \
    "    riscv_mem_read_b mem_read_b;\n"                                  \
    "    riscv_mem_write_w mem_write_w;\n"                                \
    "    riscv_mem_write_s mem_write_s;\n"                                \
    "    riscv_mem_write_b mem_write_b;\n"                                \
    "    riscv_on_ecall on_ecall;\n"                                      \
    "    riscv_on_ebreak on_ebreak;\n"                                    \
    "    bool allow_misalign;\n"                                          \
    "} riscv_io_t;\n"                                                     \
    "struct riscv_internal {\n"                                           \
    "    bool halt;\n"                                                    \
    "    riscv_io_t io;\n"                                                \
    "    riscv_word_t X[32];\n"                                           \
    "    riscv_word_t PC;\n"                                              \
    "    riscv_user_t userdata;\n"                                        \
    "    uint64_t csr_cycle;\n"                                           \
    "    uint32_t csr_time[2];\n"                                         \
    "    uint32_t csr_mstatus;\n"                                         \
    "    uint32_t csr_mtvec;\n"                                           \
    "    uint32_t csr_misa;\n"                                            \
    "    uint32_t csr_mtval;\n"                                           \
    "    uint32_t csr_mcause;\n"                                          \
    "    uint32_t csr_mscratch;\n"                                        \
    "    uint32_t csr_mepc;\n"                                            \
    "    uint32_t csr_mip;\n"                                             \
    "    uint32_t csr_mbadaddr;\n"                                        \
    "    bool compressed;\n"                                              \
    "};\n"                                                                \
    "typedef struct {\n"                                                  \
    "    uint8_t data[0x10000];\n"                                        \
    "} chunk_t;\n"                                                        \
    "typedef struct {\n"                                                  \
    "    chunk_t *chunks[0x10000];\n"                                     \
    "} memory_t;\n"                                                       \
    "typedef struct {\n"                                                  \
    "    memory_t *mem;\n"                                                \
    "    riscv_word_t break_addr;\n"                                      \
    "} state_t;\n"                                                        \
    "bool start(volatile riscv_t *rv) {\n"                                \
    "   uint32_t pc, addr, udividend, udivisor, tmp, data, mask, ures, "  \
    "a, b, jump_to;\n"                                                    \
    "   int32_t dividend, divisor, res;\n"                                \
    "   int64_t multiplicand, multiplier;\n"                              \
    "   uint64_t umultiplier;\n"                                          \
    "   memory_t *m = ((state_t *)rv->userdata)->mem;\n"                  \
    "   chunk_t *c;\n"

void trace_and_gencode(riscv_t *rv, char *gencode)
{
#define _(inst, can_branch) dispatch_table[rv_insn_##inst] = &gen_##inst;
    RISCV_INSN_LIST
#undef _

    strcat(gencode, PROLOGUE);
    set_reset(&set);
    block_t *block = cache_get(rv->cache, rv->PC);
    trace_ebb(rv, gencode, block->ir);
    strcat(gencode, "}");
}