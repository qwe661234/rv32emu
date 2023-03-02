/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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

#if RV32_HAS(GDBSTUB)
extern struct target_ops gdbstub_ops;
#endif

#include "decode.h"
#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"

#if RV32_HAS(GDBSTUB)
void rv_debug(riscv_t *rv)
{
    if (!gdbstub_init(&rv->gdbstub, &gdbstub_ops,
                      (arch_info_t){
                          .reg_num = 33,
                          .reg_byte = 4,
                          .target_desc = TARGET_RV32,
                      },
                      GDBSTUB_COMM)) {
        return;
    }

    rv->breakpoint_map = breakpoint_map_new();

    if (!gdbstub_run(&rv->gdbstub, (void *) rv))
        return;

    breakpoint_map_destroy(rv->breakpoint_map);
    gdbstub_close(&rv->gdbstub);
}
#endif /* RV32_HAS(GDBSTUB) */

/* can-branch information for each RISC-V instruction */
enum {
#define _(inst, can_branch) __rv_insn_##inst##_canbranch = can_branch,
    RISCV_INSN_LIST
#undef _
};

#ifndef PAD
#define PAD __asm__("");
#endif
#define GUARD(op) __asm__("");
#define label(x, z) op_##x##_##z:

#if defined(__aarch64__)
#define HALT_SIZE 32
#else
#define HALT_SIZE 64
#endif

#define DEF_OP_LBLS(op, PRE, code)                           \
    label(op, START) PAD label(op, ENTRY)                    \
    PRE GUARD(op) rv->X[rv_reg_zero] = 0;                    \
    ir = block->ir + ir_count++;                             \
    rv->csr_cycle++;                                         \
    CAL_PC(handle_END[ir->opcode], handle_ENTRY[ir->opcode]) \
    code;                                                    \
    if (!__rv_insn_##op##_canbranch)                         \
        rv->PC += ir->insn_len;                              \
    label(op, END);

#define DEF_HALT_LBL()                             \
    label(halt, START) PAD label(halt, ENTRY) /**/ \
        GUARD(halt) return true;                   \
    label(halt, END);

#define DEF_HALT_LBL2()                              \
    label(halt2, START) PAD label(halt2, ENTRY) /**/ \
        GUARD(halt) return false;                    \
    label(halt2, END);

#define RVOP(op, code) DEF_OP_LBLS(op, /**/, code)

#define HNDLR_LBL(opcode, level) &&op_##opcode##_##level
#define L(op, level) HNDLR_LBL(op, level)

#define OP_SIZE(OP) (char *) handle_END[OP] - (char *) handle_ENTRY[OP]
#define CAL_PC(LABEL1, LABEL2) PC += ((char *) LABEL1 - (char *) LABEL2);

static size_t *handle_SIZE = NULL;
extern jmp_buf jmpbuffer;

#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *) (addr), (char *) (addr) + (size))
#ifndef __clang__
#pragma GCC push_options
#pragma GCC optimize("Os")
#define UNLIKELY unlikely
#else
#define UNLIKELY
#endif
/* execute a basic block */
static bool emulate(riscv_t *rv, const block_t *block)
{
    static const void *handle_ENTRY[] = {
#define _(inst, can_branch) [rv_insn_##inst] = L(inst, ENTRY),
        RISCV_INSN_LIST
#undef _
    };

    static const void *handle_END[] = {
#define _(inst, can_branch) [rv_insn_##inst] = L(inst, END),
        RISCV_INSN_LIST
#undef _
    };

    if (!handle_SIZE) {
        handle_SIZE = malloc(sizeof(size_t) * 124);
        for (int i = 0; i < 124; i++)
            handle_SIZE[i] = OP_SIZE(i);
    }

    rv_insn_t volatile *ir;
    uint32_t volatile ir_count = 0;
    char *volatile PC = block->code_page;

    if (*(block->code_page))
        goto *(block->code_page);

    for (uint32_t i = 0; i < block->n_insn; i++) {
        ir = block->ir + i;
        memcpy(PC, handle_ENTRY[ir->opcode], handle_SIZE[ir->opcode]);
        sys_icache_invalidate(PC, handle_SIZE[ir->opcode]);
        CAL_PC(handle_END[ir->opcode], handle_ENTRY[ir->opcode])
    }
    memcpy(PC, &&op_halt_ENTRY, HALT_SIZE);
    sys_icache_invalidate(PC, HALT_SIZE);
    if (setjmp(jmpbuffer))
        return false;
    return true;
    RVOP(nop, /* no operation */)

    /* LUI (Load Upper Immediate) is used to build 32-bit constants and uses the
     * U-type format. LUI places the U-immediate value in the top 20 bits of the
     * destination register rd, filling in the lowest 12 bits with zeros. The
     * 32-bit result is sign-extended to 64 bits.
     */
    RVOP(lui, rv->X[ir->rd] = ir->imm;)

    /* AUIPC (Add Upper Immediate to PC) is used to build pc-relative addresses
     * and uses the U-type format. AUIPC forms a 32-bit offset from the 20-bit
     * U-immediate, filling in the lowest 12 bits with zeros, adds this offset
     * to the address of the AUIPC instruction, then places the result in
     * register rd.
     */
    RVOP(auipc, rv->X[ir->rd] = ir->imm + rv->PC;)

    /* JAL: Jump and Link
     * store successor instruction address into rd.
     * add next J imm (offset) to pc.
     */
    RVOP(jal, {
        const uint32_t pc = rv->PC;
        /* Jump */
        rv->PC += ir->imm;
        /* link with return address */
        if (ir->rd)
            rv->X[ir->rd] = pc + ir->insn_len;
        /* check instruction misaligned */
        if (UNLIKELY(insn_is_misaligned(rv->PC))) {
            rv->compressed = false;
            rv->exception_handler[0](rv, pc);
            rv->ret_false();
        }
    })

    /* JALR: Jump and Link Register
     * The indirect jump instruction JALR uses the I-type encoding. The
     * target address is obtained by adding the sign-extended 12-bit
     * I-immediate to the register rs1, then setting the least-significant
     * bit of the result to zero. The address of the instruction following
     * the jump (pc+4) is written to register rd. Register x0 can be used as
     * the destination if the result is not required.
     */
    RVOP(jalr, {
        const uint32_t pc = rv->PC;
        /* jump */
        rv->PC = (rv->X[ir->rs1] + ir->imm) & ~1U;
        /* link */
        if (ir->rd)
            rv->X[ir->rd] = pc + ir->insn_len;
        /* check instruction misaligned */
        if (UNLIKELY(insn_is_misaligned(rv->PC))) {
            rv->compressed = false;
            rv->exception_handler[0](rv, pc);
            rv->ret_false();
        }
    })

    /* BEQ: Branch if Equal */
    RVOP(beq, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] == rv->X[ir->rs2]) {
            rv->PC = pc + ir->imm;

            /* check instruction misaligned */
            if (UNLIKELY(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv->exception_handler[0](rv, pc);
                rv->ret_false();
            }
        } else
            rv->PC += ir->insn_len;
    })

    /* BNE: Branch if Not Equal */
    RVOP(bne, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] != rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (UNLIKELY(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv->exception_handler[0](rv, pc);
                rv->ret_false();
            }
        } else
            rv->PC += ir->insn_len;
    })

    /* BLT: Branch if Less Than */
    RVOP(blt, {
        const uint32_t pc = rv->PC;
        if ((int32_t) rv->X[ir->rs1] < (int32_t) rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (UNLIKELY(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv->exception_handler[0](rv, pc);
                rv->ret_false();
            }
        } else
            rv->PC += ir->insn_len;
    })

    /* BGE: Branch if Greater Than */
    RVOP(bge, {
        const uint32_t pc = rv->PC;
        if ((int32_t) rv->X[ir->rs1] >= (int32_t) rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (UNLIKELY(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv->exception_handler[0](rv, pc);
                rv->ret_false();
            }
        } else
            rv->PC += ir->insn_len;
    })

    /* BLTU: Branch if Less Than Unsigned */
    RVOP(bltu, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] < rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (UNLIKELY(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv->exception_handler[0](rv, pc);
                rv->ret_false();
            }
        } else
            rv->PC += ir->insn_len;
    })

    /* BGEU: Branch if Greater Than Unsigned */
    RVOP(bgeu, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] >= rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (UNLIKELY(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv->exception_handler[0](rv, pc);
                rv->ret_false();
            }
        } else
            rv->PC += ir->insn_len;
    })

    /* LB: Load Byte */
    RVOP(lb, {
        rv->X[ir->rd] =
            sign_extend_b(rv->io.mem_read_b(rv, rv->X[ir->rs1] + ir->imm));
    })

    /* LH: Load Halfword */
    RVOP(lh, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (UNLIKELY(addr & 1)) {
            rv->compressed = false;
            rv->exception_handler[3](rv, addr);
            rv->ret_false();
        }
        rv->X[ir->rd] = sign_extend_h(rv->io.mem_read_s(rv, addr));
    })

    /* LW: Load Word */
    RVOP(lw, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (UNLIKELY(addr & 3)) {
            rv->compressed = false;
            rv->exception_handler[3](rv, addr);
            rv->ret_false();
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
    })

    /* LBU: Load Byte Unsigned */
    RVOP(lbu, rv->X[ir->rd] = rv->io.mem_read_b(rv, rv->X[ir->rs1] + ir->imm);)

    /* LHU: Load Halfword Unsigned */
    RVOP(lhu, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (UNLIKELY(addr & 1)) {
            rv->compressed = false;
            rv->exception_handler[3](rv, addr);
            rv->ret_false();
        }
        rv->X[ir->rd] = rv->io.mem_read_s(rv, addr);
    })

    /* SB: Store Byte */
    RVOP(sb, rv->io.mem_write_b(rv, rv->X[ir->rs1] + ir->imm, rv->X[ir->rs2]);)

    /* SH: Store Halfword */
    RVOP(sh, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (UNLIKELY(addr & 1)) {
            rv->compressed = false;
            rv->exception_handler[4](rv, addr);
            rv->ret_false();
        }
        rv->io.mem_write_s(rv, addr, rv->X[ir->rs2]);
    })

    /* SW: Store Word */
    RVOP(sw, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (UNLIKELY(addr & 3)) {
            rv->compressed = false;
            rv->exception_handler[4](rv, addr);
            rv->ret_false();
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
    })

    /* ADDI (Add Immediate) adds the sign-extended 12-bit immediate to register
     * rs1. Arithmetic overflow is ignored and the result is simply the low XLEN
     * bits of the result. ADDI rd, rs1, 0 is used to implement the MV rd, rs1
     * assembler pseudo-instruction.
     */
    RVOP(addi, { rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + ir->imm; })

    /* SLTI (Set on Less Than Immediate) places the value 1 in register rd if
     * register rs1 is less than the signextended immediate when both are
     * treated as signed numbers, else 0 is written to rd.
     */
    RVOP(slti, rv->X[ir->rd] = ((int32_t) (rv->X[ir->rs1]) < ir->imm) ? 1 : 0;)

    /* SLTIU (Set on Less Than Immediate Unsigned) places the value 1 in
     * register rd if register rs1 is less than the immediate when both are
     * treated as unsigned numbers, else 0 is written to rd.
     */
    RVOP(sltiu, rv->X[ir->rd] = (rv->X[ir->rs1] < (uint32_t) ir->imm) ? 1 : 0;)

    /* XORI: Exclusive OR Immediate */
    RVOP(xori, rv->X[ir->rd] = rv->X[ir->rs1] ^ ir->imm;)

    /* ORI: OR Immediate */
    RVOP(ori, rv->X[ir->rd] = rv->X[ir->rs1] | ir->imm;)

    /* ANDI (AND Immediate) performs bitwise AND on register rs1 and the
     * sign-extended 12-bit immediate and place the result in rd.
     */
    RVOP(andi, rv->X[ir->rd] = rv->X[ir->rs1] & ir->imm;)

    /* SLLI (Shift Left Logical) performs logical left shift on the value in
     * register rs1 by the shift amount held in the lower 5 bits of the
     * immediate.
     */
    RVOP(slli, { rv->X[ir->rd] = rv->X[ir->rs1] << (ir->imm & 0x1f); })

    /* SRLI (Shift Right Logical) performs logical right shift on the value in
     * register rs1 by the shift amount held in the lower 5 bits of the
     * immediate.
     */
    RVOP(srli, rv->X[ir->rd] = rv->X[ir->rs1] >> (ir->imm & 0x1f);)

    /* SRAI (Shift Right Arithmetic) performs arithmetic right shift on the
     * value in register rs1 by the shift amount held in the lower 5 bits of the
     * immediate.
     */
    RVOP(srai, rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (ir->imm & 0x1f);)

    /* ADD */
    RVOP(add, {
        rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + (int32_t) (rv->X[ir->rs2]);
    })

    /* SUB: Substract */
    RVOP(sub, {
        rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) - (int32_t) (rv->X[ir->rs2]);
    })

    /* SLL: Shift Left Logical */
    RVOP(sll, rv->X[ir->rd] = rv->X[ir->rs1] << (rv->X[ir->rs2] & 0x1f);)

    /* SLT: Set on Less Than */
    RVOP(slt, {
        rv->X[ir->rd] =
            ((int32_t) (rv->X[ir->rs1]) < (int32_t) (rv->X[ir->rs2])) ? 1 : 0;
    })

    /* SLTU: Set on Less Than Unsigned */
    RVOP(sltu, rv->X[ir->rd] = (rv->X[ir->rs1] < rv->X[ir->rs2]) ? 1 : 0;)

    /* XOR: Exclusive OR */
    RVOP(xor, rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2];)

    /* SRL: Shift Right Logical */
    RVOP(srl, rv->X[ir->rd] = rv->X[ir->rs1] >> (rv->X[ir->rs2] & 0x1f);)

    /* SRA: Shift Right Arithmetic */
    RVOP(sra, {
        rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (rv->X[ir->rs2] & 0x1f);
    })

    /* OR */
    RVOP(or, rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2];)

    /* AND */
    RVOP(and, rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2];)

    /* ECALL: Environment Call */
    RVOP(ecall, {
        rv->compressed = false;
        rv->io.on_ecall(rv); /* increment the cycles csr */
    })

    /* EBREAK: Environment Break */
    RVOP(ebreak, {
        rv->compressed = false;
        rv->io.on_ebreak(rv); /* increment the cycles csr */
    })

    /* WFI: Wait for Interrupt */
    RVOP(wfi, return false;)

    /* URET: return from traps in U-mode */
    RVOP(uret, return false;)

    /* SRET: return from traps in S-mode */
    RVOP(sret, return false;)

    /* HRET: return from traps in H-mode */
    RVOP(hret, return false;)

    /* MRET: return from traps in U-mode */
    RVOP(mret, { rv->PC = rv->csr_mepc; })

    /* RV32 Zifencei Standard Extension */
#if RV32_HAS(Zifencei)
    RVOP(fencei, { /* FIXME: fill real implementations */
                   rv->PC += ir->insn_len;
    });
#endif

    /* RV32 Zicsr Standard Extension */
#if RV32_HAS(Zicsr)
    /* CSRRW: Atomic Read/Write CSR */
    RVOP(csrrw, {
        uint32_t tmp = rv->csr_handler[0](rv, ir->imm, rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRS: Atomic Read and Set Bits in CSR */
    RVOP(csrrs, {
        uint32_t tmp = rv->csr_handler[1](rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRC: Atomic Read and Clear Bits in CSR */
    RVOP(csrrc, {
        uint32_t tmp = rv->csr_handler[2](
            rv, ir->imm, (ir->rs1 == rv_reg_zero) ? ~0U : rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRWI */
    RVOP(csrrwi, {
        uint32_t tmp = rv->csr_handler[0](rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRSI */
    RVOP(csrrsi, {
        uint32_t tmp = rv->csr_handler[1](rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRCI */
    RVOP(csrrci, {
        uint32_t tmp = rv->csr_handler[2](rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })
#endif /* RV32_HAS(Zicsr) */

    /* RV32M Standard Extension */
#if RV32_HAS(EXT_M)
    /* MUL: Multiply */
    RVOP(mul,
         rv->X[ir->rd] = (int32_t) rv->X[ir->rs1] * (int32_t) rv->X[ir->rs2];)

    /* MULH: Multiply High Signed Signed */
    RVOP(mulh, {
        const int64_t a = (int32_t) rv->X[ir->rs1];
        const int64_t b = (int32_t) rv->X[ir->rs2];
        rv->X[ir->rd] = ((uint64_t) (a * b)) >> 32;
    })

    /* MULHSU: Multiply High Signed Unsigned */
    RVOP(mulhsu, {
        const int64_t a = (int32_t) rv->X[ir->rs1];
        const uint64_t b = rv->X[ir->rs2];
        rv->X[ir->rd] = ((uint64_t) (a * b)) >> 32;
    })

    /* MULHU: Multiply High Unsigned Unsigned */
    RVOP(mulhu, {
        rv->X[ir->rd] =
            ((uint64_t) rv->X[ir->rs1] * (uint64_t) rv->X[ir->rs2]) >> 32;
    })

    /* DIV: Divide Signed */
    RVOP(div, {
        const int32_t dividend = (int32_t) rv->X[ir->rs1];
        const int32_t divisor = (int32_t) rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? ~0U
                        : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                            ? rv->X[ir->rs1] /* overflow */
                            : (unsigned int) (dividend / divisor);
    })

    /* DIVU: Divide Unsigned */
    RVOP(divu, {
        const uint32_t dividend = rv->X[ir->rs1];
        const uint32_t divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? ~0U : dividend / divisor;
    })

    /* REM: Remainder Signed */
    RVOP(rem, {
        const int32_t dividend = rv->X[ir->rs1];
        const int32_t divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? dividend
                        : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                            ? 0 /* overflow */
                            : (dividend % divisor);
    })

    /* REMU: Remainder Unsigned */
    RVOP(remu, {
        const uint32_t dividend = rv->X[ir->rs1];
        const uint32_t divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? dividend : dividend % divisor;
    })
#endif /* RV32_HAS(EXT_M) */

    /* RV32A Standard Extension
     * At present, AMO is not implemented atomically because the emulated
     * RISC-V core just runs on single thread, and no out-of-order execution
     * happens. In addition, rl/aq are not handled.
     */
#if RV32_HAS(EXT_A)
    /* LR.W: Load Reserved */
    RVOP(lrw, {
        /* skip registration of the 'reservation set'
         * FIXME: uimplemented
         */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, rv->X[ir->rs1]);
    })

    /* SC.W: Store Conditional */
    RVOP(scw, {
        /* assume the 'reservation set' is valid
         * FIXME: unimplemented
         */
        rv->io.mem_write_w(rv, rv->X[ir->rs1], rv->X[ir->rs2]);
        rv->X[ir->rd] = 0;
    })

    /* AMOSWAP.W: Atomic Swap */
    RVOP(amoswapw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        rv->io.mem_write_s(rv, ir->rs1, rv->X[ir->rs2]);
    })

    /* AMOADD.W: Atomic ADD */
    RVOP(amoaddw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = (int32_t) rv->X[ir->rd] + (int32_t) rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOXOR.W: Atomix XOR */
    RVOP(amoxorw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] ^ rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOAND.W: Atomic AND */
    RVOP(amoandw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] & rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOOR.W: Atomic OR */
    RVOP(amoorw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] | rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMIN.W: Atomic MIN */
    RVOP(amominw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t a = rv->X[ir->rd];
        const int32_t b = rv->X[ir->rs2];
        const int32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMAX.W: Atomic MAX */
    RVOP(amomaxw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t a = rv->X[ir->rd];
        const int32_t b = rv->X[ir->rs2];
        const int32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMINU.W */
    RVOP(amominuw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const uint32_t a = rv->X[ir->rd];
        const uint32_t b = rv->X[ir->rs2];
        const uint32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMAXU.W */
    RVOP(amomaxuw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const uint32_t a = rv->X[ir->rd];
        const uint32_t b = rv->X[ir->rs2];
        const uint32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })
#endif /* RV32_HAS(EXT_A) */

    /* RV32F Standard Extension */
#if RV32_HAS(EXT_F)
    /* FLW */
    RVOP(flw, {
        /* copy into the float register */
        const uint32_t data = rv->io.mem_read_w(rv, rv->X[ir->rs1] + ir->imm);
        memcpy(rv->F + ir->rd, &data, 4);
    })

    /* FSW */
    RVOP(fsw, {
        /* copy from float registers */
        uint32_t data;
        memcpy(&data, (const void *) (rv->F + ir->rs2), 4);
        rv->io.mem_write_w(rv, rv->X[ir->rs1] + ir->imm, data);
    })

    /* FMADD.S */
    RVOP(fmadds,
         rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] + rv->F[ir->rs3];)

    /* FMSUB.S */
    RVOP(fmsubs,
         rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] - rv->F[ir->rs3];)

    /* FNMSUB.S */
    RVOP(fnmsubs,
         rv->F[ir->rd] = rv->F[ir->rs3] - (rv->F[ir->rs1] * rv->F[ir->rs2]);)

    /* FNMADD.S */
    RVOP(fnmadds,
         rv->F[ir->rd] = -(rv->F[ir->rs1] * rv->F[ir->rs2]) - rv->F[ir->rs3];)

    /* FADD.S */
    RVOP(fadds, {
        if (isnanf(rv->F[ir->rs1]) || isnanf(rv->F[ir->rs2]) ||
            isnanf(rv->F[ir->rs1] + rv->F[ir->rs2])) {
            /* raise invalid operation */
            rv->F_int[ir->rd] = RV_NAN;
            /* F_int is the integer shortcut of F */
            rv->csr_fcsr |= FFLAG_INVALID_OP;
        } else {
            rv->F[ir->rd] = rv->F[ir->rs1] + rv->F[ir->rs2];
        }
        if (isinff(rv->F[ir->rd])) {
            rv->csr_fcsr |= FFLAG_OVERFLOW;
            rv->csr_fcsr |= FFLAG_INEXACT;
        }
    })

    /* FSUB.S */
    RVOP(fsubs, {
        if (isnanf(rv->F[ir->rs1]) || isnanf(rv->F[ir->rs2])) {
            rv->F_int[ir->rd] = RV_NAN;
        } else {
            rv->F[ir->rd] = rv->F[ir->rs1] - rv->F[ir->rs2];
        }
    })

    /* FMUL.S */
    RVOP(fmuls, rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2];)

    /* FDIV.S */
    RVOP(fdivs, rv->F[ir->rd] = rv->F[ir->rs1] / rv->F[ir->rs2];)

    /* FSQRT.S */
    RVOP(fsqrts, rv->F[ir->rd] = sqrtf(rv->F[ir->rs1]);)

    /* FSGNJ.S */
    RVOP(fsgnjs, {
        uint32_t f1;
        uint32_t f2;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        uint32_t res = (f1 & ~FMASK_SIGN) | (f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
    })

    /* FSGNJN.S */
    RVOP(fsgnjns, {
        uint32_t f1;
        uint32_t f2;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        uint32_t res = (f1 & ~FMASK_SIGN) | (~f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
    })

    /* FSGNJX.S */
    RVOP(fsgnjxs, {
        uint32_t f1;
        uint32_t f2;
        uint32_t res;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        res = f1 ^ (f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
    })

    /* FMIN.S */
    RVOP(fmins, {
        /* In IEEE754-201x, fmin(x, y) return
         * - min(x,y) if both numbers are not NaN
         * - if one is NaN and another is a number, return the number
         * - if both are NaN, return NaN
         * When input is signaling NaN, raise invalid operation
         */
        uint32_t x;
        uint32_t y;
        memcpy(&x, rv->F + ir->rs1, 4);
        memcpy(&y, rv->F + ir->rs2, 4);
        if (is_nan(x) || is_nan(y)) {
            if (is_snan(x) || is_snan(y))
                rv->csr_fcsr |= FFLAG_INVALID_OP;
            if (is_nan(x) && !is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs2];
            } else if (!is_nan(x) && is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs1];
            } else {
                rv->F_int[ir->rd] = RV_NAN;
            }
        } else {
            uint32_t a_sign;
            uint32_t b_sign;
            a_sign = x & FMASK_SIGN;
            b_sign = y & FMASK_SIGN;
            if (a_sign != b_sign) {
                rv->F[ir->rd] = a_sign ? rv->F[ir->rs1] : rv->F[ir->rs2];
            } else {
                rv->F[ir->rd] = (rv->F[ir->rs1] < rv->F[ir->rs2])
                                    ? rv->F[ir->rs1]
                                    : rv->F[ir->rs2];
            }
        }
    })

    /* FMAX.S */
    RVOP(fmaxs, {
        uint32_t x;
        uint32_t y;
        memcpy(&x, rv->F + ir->rs1, 4);
        memcpy(&y, rv->F + ir->rs2, 4);
        if (is_nan(x) || is_nan(y)) {
            if (is_snan(x) || is_snan(y))
                rv->csr_fcsr |= FFLAG_INVALID_OP;
            if (is_nan(x) && !is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs2];
            } else if (!is_nan(x) && is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs1];
            } else {
                rv->F_int[ir->rd] = RV_NAN;
            }
        } else {
            uint32_t a_sign;
            uint32_t b_sign;
            a_sign = x & FMASK_SIGN;
            b_sign = y & FMASK_SIGN;
            if (a_sign != b_sign) {
                rv->F[ir->rd] = a_sign ? rv->F[ir->rs2] : rv->F[ir->rs1];
            } else {
                rv->F[ir->rd] = (rv->F[ir->rs1] > rv->F[ir->rs2])
                                    ? rv->F[ir->rs1]
                                    : rv->F[ir->rs2];
            }
        }
    })

    /* FCVT.W.S */
    RVOP(fcvtws, rv->X[ir->rd] = (int32_t) rv->F[ir->rs1];)

    /* FCVT.WU.S */
    RVOP(fcvtwus, rv->X[ir->rd] = (uint32_t) rv->F[ir->rs1];)

    /* FMV.X.W */
    RVOP(fmvxw, memcpy(rv->X + ir->rd, rv->F + ir->rs1, 4);)

    /* FEQ.S performs a quiet comparison: it only sets the invalid
     * operation exception flag if either input is a signaling NaN.
     */
    RVOP(feqs, rv->X[ir->rd] = (rv->F[ir->rs1] == rv->F[ir->rs2]) ? 1 : 0;)

    /* FLT.S and FLE.S perform what the IEEE 754-2008 standard refers
     * to as signaling comparisons: that is, they set the invalid
     * operation exception flag if either input is NaN.
     */
    RVOP(flts, rv->X[ir->rd] = (rv->F[ir->rs1] < rv->F[ir->rs2]) ? 1 : 0;)

    /* FLE.S */
    RVOP(fles, rv->X[ir->rd] = (rv->F[ir->rs1] <= rv->F[ir->rs2]) ? 1 : 0;)

    /* FCLASS.S */
    RVOP(fclasss, {
        uint32_t bits;
        memcpy(&bits, rv->F + ir->rs1, 4);
        rv->X[ir->rd] = calc_fclass(bits);
    })

    /* FCVT.S.W */
    RVOP(fcvtsw, rv->F[ir->rd] = (float) (int32_t) rv->X[ir->rs1];)

    /* FCVT.S.WU */
    RVOP(fcvtswu, rv->F[ir->rd] = (float) (uint32_t) rv->X[ir->rs1];)

    /* FMV.W.X */
    RVOP(fmvwx, memcpy(rv->F + ir->rd, rv->X + ir->rs1, 4);)
#endif /* RV32_HAS(EXT_F) */

    /* RV32C Standard Extension */
#if RV32_HAS(EXT_C)
    /* C.ADDI4SPN is a CIW-format instruction that adds a zero-extended
     * non-zero immediate, scaledby 4, to the stack pointer, x2, and
     * writes the result to rd'. This instruction is used to generate
     * pointers to stack-allocated variables, and expands to addi rd',
     * x2, nzuimm[9:2].
     */
    RVOP(caddi4spn, rv->X[ir->rd] = rv->X[2] + (uint16_t) ir->imm;)

    /* C.LW loads a 32-bit value from memory into register rd'. It
     * computes an ffective address by adding the zero-extended offset,
     * scaled by 4, to the base address in register rs1'. It expands to
     * # lw rd', offset[6:2](rs1').
     */
    RVOP(clw, {
        const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv->exception_handler[3](rv, addr);
            rv->ret_false();
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
    })

    /* C.SW stores a 32-bit value in register rs2' to memory. It computes
     * an effective address by adding the zero-extended offset, scaled by
     * 4, to the base address in register rs1'.
     * It expands to sw rs2', offset[6:2](rs1')
     */
    RVOP(csw, {
        const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv->exception_handler[4](rv, addr);
            rv->ret_false();
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
    })

    /* C.NOP */
    RVOP(cnop, /* nothing */)

    /* C.ADDI adds the non-zero sign-extended 6-bit immediate to the
     * value in register rd then writes the result to rd. C.ADDI expands
     * into addi rd, rd, nzimm[5:0]. C.ADDI is only valid when rd̸=x0.
     * The code point with both rd=x0 and nzimm=0 encodes the C.NOP
     * instruction; the remaining code points with either rd=x0 or
     * nzimm=0 encode HINTs.
     */
    RVOP(caddi, rv->X[ir->rd] += (int16_t) ir->imm;)

    /* C.JAL */
    RVOP(cjal, {
        rv->X[1] = rv->PC + ir->insn_len;
        rv->PC += ir->imm;
        if (rv->PC & 0x1) {
            rv->compressed = true;
            rv->exception_handler[0](rv, rv->PC);
            rv->ret_false();
        }
    })

    /* C.LI loads the sign-extended 6-bit immediate, imm, into
     * register rd.
     * C.LI expands into addi rd, x0, imm[5:0].
     * C.LI is only valid when rd=x0; the code points with rd=x0 encode
     * HINTs.
     */
    RVOP(cli, rv->X[ir->rd] = ir->imm;)

    /* C.ADDI16SP is used to adjust the stack pointer in procedure
     * prologues and epilogues.
     * It expands into addi x2, x2, nzimm[9:4].
     * C.ADDI16SP is only valid when nzimm̸=0; the code point with
     * nzimm=0 is reserved.
     */
    RVOP(caddi16sp, rv->X[ir->rd] += ir->imm;)

    /* C.LUI loads the non-zero 6-bit immediate field into bits 17–12 of
     * the destination register, clears the bottom 12 bits, and
     * sign-extends bit 17 into all higher bits of the destination.
     * C.LUI expands into lui rd, nzimm[17:12].
     * C.LUI is only valid when rd̸={x0, x2}, and when the immediate is
     * not equal to zero.
     */
    RVOP(clui, rv->X[ir->rd] = ir->imm;)

    /* C.SRLI is a CB-format instruction that performs a logical right
     * shift of the value in register rd' then writes the result to rd'.
     * The shift amount is encoded in the shamt field. C.SRLI expands
     * into srli rd', rd', shamt[5:0].
     */
    RVOP(csrli, rv->X[ir->rs1] >>= ir->shamt;)

    /* C.SRAI is defined analogously to C.SRLI, but instead performs an
     * arithmetic right shift.
     * C.SRAI expands to srai rd', rd', shamt[5:0].
     */
    RVOP(csrai, {
        const uint32_t mask = 0x80000000 & rv->X[ir->rs1];
        rv->X[ir->rs1] >>= ir->shamt;
        for (unsigned int i = 0; i < ir->shamt; ++i)
            rv->X[ir->rs1] |= mask >> i;
    })

    /* C.ANDI is a CB-format instruction that computes the bitwise AND of
     * the value in register rd' and the sign-extended 6-bit immediate,
     * then writes the result to rd'.
     * C.ANDI expands to andi rd', rd', imm[5:0].
     */
    RVOP(candi, rv->X[ir->rs1] &= ir->imm;)

    /* C.SUB */
    RVOP(csub, rv->X[ir->rd] = rv->X[ir->rs1] - rv->X[ir->rs2];)

    /* C.XOR */
    RVOP(cxor, rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2];)

    /* C.OR */
    RVOP(cor, rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2];)

    /* C.AND */
    RVOP(cand, rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2];)

    /* C.BEQZ performs conditional control transfers. The offset is
     * sign-extended and added to the pc to form the branch target
     * address. It can therefore target a ±256 B range. C.BEQZ takes the
     * branch if the value in register rs1' is zero.
     * It expands to beq rs1', x0, offset[8:1].
     */
    RVOP(cbeqz, {
        if (rv->PC)
            rv->PC += (!rv->X[ir->rs1]) ? (uint32_t) ir->imm : ir->insn_len;
    })

    RVOP(cbnez,
         { rv->PC += (rv->X[ir->rs1]) ? (uint32_t) ir->imm : ir->insn_len; })

    /* C.J performs an unconditional control transfer. The offset is
     * sign-extended and added to the pc to form the jump target address.
     * C.J can therefore target a ±2 KiB range.
     * C.J expands to jal x0, offset[11:1].
     */
    RVOP(cj, {
        rv->PC += ir->imm;
        if (rv->PC & 0x1) {
            rv->compressed = true;
            rv->exception_handler[0](rv, rv->PC);
            rv->ret_false();
        }
    })

    /* C.SLLI is a CI-format instruction that performs a logical left
     * shift of the value in register rd then writes the result to rd.
     * The shift amount is encoded in the shamt field.
     * C.SLLI expands into slli rd, rd, shamt[5:0].
     */
    RVOP(cslli, rv->X[ir->rd] <<= (uint8_t) ir->imm;)

    /* C.JR */
    RVOP(cjr, { rv->PC = rv->X[ir->rs1]; })

    /* C.MV */
    RVOP(cmv, rv->X[ir->rd] = rv->X[ir->rs2];)

    /* C.EBREAK */
    RVOP(cebreak, {
        rv->compressed = true;
        rv->io.on_ebreak(rv);
    })
    /* C.LWSP */
    RVOP(clwsp, {
        const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv->exception_handler[3](rv, addr);
            rv->ret_false();
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
    })

    /* C.SWSP */
    RVOP(cswsp, {
        const uint32_t addr = rv->X[2] + ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv->exception_handler[4](rv, addr);
            rv->ret_false();
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
    })
    /* C.JALR */
    RVOP(cjalr, {
        /* Unconditional jump and store PC+2 to ra */
        const int32_t jump_to = rv->X[ir->rs1];
        rv->X[rv_reg_ra] = rv->PC + ir->insn_len;
        rv->PC = jump_to;
        if (rv->PC & 0x1) {
            rv->compressed = true;
            rv->exception_handler[0](rv, rv->PC);
            rv->ret_false();
        }
    })

    /* C.ADD adds the values in registers rd and rs2 and writes the
     * result to register rd.
     * C.ADD expands into add rd, rd, rs2.
     * C.ADD is only valid when rs2=x0; the code points with rs2=x0
     * correspond to the C.JALR and C.EBREAK instructions. The code
     * points with rs2=x0 and rd=x0 are HINTs.
     */
    RVOP(cadd, rv->X[ir->rd] = rv->X[ir->rs1] + rv->X[ir->rs2];)

#endif

    DEF_HALT_LBL()
}
#ifndef __clang__
#pragma GCC pop_options
#endif

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

/* hash function is used when mapping address into the block map */
static uint32_t hash(size_t k)
{
    k ^= k << 21;
    k ^= k >> 17;
#if (SIZE_MAX > 0xFFFFFFFF)
    k ^= k >> 35;
    k ^= k >> 51;
#endif
    return k;
}

/* allocate a basic block */
static block_t *block_alloc(const uint8_t bits)
{
    block_t *block = malloc(sizeof(struct block));
    block->insn_capacity = 1 << bits;
    block->n_insn = 0;
    block->predict = NULL;
    block->ir = malloc(block->insn_capacity * sizeof(rv_insn_t));
    return block;
}

/* insert a block into block map */
static void block_insert(block_map_t *map, const block_t *block)
{
    assert(map && block);
    const uint32_t mask = map->block_capacity - 1;
    uint32_t index = hash(block->pc_start);

    /* insert into the block map */
    for (;; index++) {
        if (!map->map[index & mask]) {
            map->map[index & mask] = (block_t *) block;
            break;
        }
    }
    map->size++;
}

/* try to locate an already translated block in the block map */
static block_t *block_find(const block_map_t *map, const uint32_t addr)
{
    assert(map);
    uint32_t index = hash(addr);
    const uint32_t mask = map->block_capacity - 1;

    /* find block in block map */
    for (;; index++) {
        block_t *block = map->map[index & mask];
        if (!block)
            return NULL;

        if (block->pc_start == addr)
            return block;
    }
    return NULL;
}

static void block_translate(riscv_t *rv, block_t *block)
{
    block->pc_start = block->pc_end = rv->PC;

    /* translate the basic block */
    while (block->n_insn < block->insn_capacity) {
        rv_insn_t *ir = block->ir + block->n_insn;
        memset(ir, 0, sizeof(rv_insn_t));

        /* fetch the next instruction */
        const uint32_t insn = rv->io.mem_ifetch(rv, block->pc_end);

        /* decode the instruction */
        if (!rv_decode(ir, insn)) {
            rv->compressed = (ir->insn_len == INSN_16);
            rv->exception_handler[1](rv, insn);
            break;
        }
        /* compute the end of pc */
        block->pc_end += ir->insn_len;
        block->n_insn++;

        /* stop on branch */
        if (insn_is_branch(ir->opcode))
            break;
    }
}

int count = 0;
block_t *barray[1024];
static block_t *block_find_or_translate(riscv_t *rv, block_t *prev)
{
    block_map_t *map = &rv->block_map;
    /* lookup the next block in the block map */
    block_t *next = block_find(map, rv->PC);
    // block_t *next = NULL;
    if (!next) {
        if (map->size * 1.25 > map->block_capacity) {
            count = 0;
            memset(barray, 0, 1024 * sizeof(block_t *));
            block_map_clear(map);
            prev = NULL;
        }

        /* allocate a new block */
        next = block_alloc(10);

        /* translate the basic block */
        block_translate(rv, next);

        /* insert the block into block map */
        block_insert(&rv->block_map, next);

        /* initialize code page */
        next->code_page = NULL;
        /* update the block prediction
         * When we translate a new block, the block predictor may benefit,
         * but when it is updated after we find a particular block, it may
         * penalize us significantly.
         */
        if (prev)
            prev->predict = next;
    }

    if (!next->code_page) {
        if (barray[count])
            barray[count]->code_page = NULL;
        memset(rv->code_page[count], 0, 4096);
        barray[count] = next;
        next->code_page = rv->code_page[count];
        count = (count + 1) % 1024;
        emulate(rv, next);
    }

    return next;
}

void rv_step(riscv_t *rv, int32_t cycles)
{
    assert(rv);

    /* find or translate a block for starting PC */
    block_t *prev = NULL;

    const uint64_t cycles_target = rv->csr_cycle + cycles;

    /* loop until we hit out cycle target */
    while (rv->csr_cycle < cycles_target && !rv->halt) {
        block_t *block;

        /* try to predict the next block */
        if (prev && prev->predict && prev->predict->pc_start == rv->PC) {
            block = prev->predict;
            if (!block->code_page) {
                if (barray[count])
                    barray[count]->code_page = NULL;
                memset(rv->code_page[count], 0, 4096);
                barray[count] = block;
                block->code_page = rv->code_page[count];
                count = (count + 1) % 1024;
                emulate(rv, block);
            }
        } else {
            /* lookup the next block in block map or translate a new block,
             * and move onto the next block.
             */
            block = block_find_or_translate(rv, prev);
        }

        /* we should have a block by now */
        assert(block);
        /* execute the block */
        if (UNLIKELY(!emulate(rv, block)))
            break;
        prev = block;
    }
}

void ebreak_handler(riscv_t *rv)
{
    assert(rv);
    rv->exception_handler[2](rv, rv->PC);
}

void ecall_handler(riscv_t *rv)
{
    assert(rv);
    rv->exception_handler[5](rv, 0);
    syscall_handler(rv);
}