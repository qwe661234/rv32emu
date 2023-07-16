#define PROLOGUE \
"#include <stdint.h>\n"\
"#include <stdbool.h>\n"\
"enum {"\
"    rv_reg_zero = 0,     rv_reg_ra,           rv_reg_sp,           rv_reg_gp,           rv_reg_tp,           rv_reg_t0,           rv_reg_t1,           rv_reg_t2,"\
"    rv_reg_s0,     rv_reg_s1,"\
"    rv_reg_a0,     rv_reg_a1,"\
"    rv_reg_a2,     rv_reg_a3,"\
"    rv_reg_a4,"\
"    rv_reg_a5,"\
"    rv_reg_a6,"\
"    rv_reg_a7,"\
"    rv_reg_s2,     rv_reg_s3,"\
"    rv_reg_s4,"\
"    rv_reg_s5,"\
"    rv_reg_s6,"\
"    rv_reg_s7,"\
"    rv_reg_s8,"\
"    rv_reg_s9,"\
"    rv_reg_s10,"\
"    rv_reg_s11,"\
"    rv_reg_t3,     rv_reg_t4,"\
"    rv_reg_t5,"\
"    rv_reg_t6,"\
"    RV_N_REGS, };"\
""\
"typedef struct riscv_internal riscv_t;"\
"typedef void *riscv_user_t;"\
""\
"typedef uint32_t riscv_word_t;"\
"typedef uint16_t riscv_half_t;"\
"typedef uint8_t riscv_byte_t;"\
"typedef uint32_t riscv_exception_t;"\
"typedef float riscv_float_t;"\
""\
"typedef riscv_word_t (*riscv_mem_ifetch)(riscv_t *rv, riscv_word_t addr);"\
"typedef riscv_word_t (*riscv_mem_read_w)(riscv_t *rv, riscv_word_t addr);"\
"typedef riscv_half_t (*riscv_mem_read_s)(riscv_t *rv, riscv_word_t addr);"\
"typedef riscv_byte_t (*riscv_mem_read_b)(riscv_t *rv, riscv_word_t addr);"\
""\
"typedef void (*riscv_mem_write_w)(riscv_t *rv,"\
"                                  riscv_word_t addr,"\
"                                  riscv_word_t data);"\
"typedef void (*riscv_mem_write_s)(riscv_t *rv,"\
"                                  riscv_word_t addr,"\
"                                  riscv_half_t data);"\
"typedef void (*riscv_mem_write_b)(riscv_t *rv,"\
"                                  riscv_word_t addr,"\
"                                  riscv_byte_t data);"\
""\
"typedef void (*riscv_on_ecall)(riscv_t *rv);"\
"typedef void (*riscv_on_ebreak)(riscv_t *rv);"\
""\
"typedef struct {"\
"        riscv_mem_ifetch mem_ifetch;"\
"    riscv_mem_read_w mem_read_w;"\
"    riscv_mem_read_s mem_read_s;"\
"    riscv_mem_read_b mem_read_b;"\
""\
"        riscv_mem_write_w mem_write_w;"\
"    riscv_mem_write_s mem_write_s;"\
"    riscv_mem_write_b mem_write_b;"\
""\
"        riscv_on_ecall on_ecall;"\
"    riscv_on_ebreak on_ebreak;"\
""\
"        bool allow_misalign;"\
"} riscv_io_t;"\
"struct riscv_internal {"\
"    bool halt;"\
""\
"        riscv_io_t io;"\
""\
"        riscv_word_t X[RV_N_REGS];"\
"    riscv_word_t PC;"\
""\
"        riscv_user_t userdata;"\
""\
""\
""\
"        union {"\
"        riscv_float_t F[RV_N_REGS];"\
"        uint32_t F_int[RV_N_REGS];     };"\
"    uint32_t csr_fcsr;"\
""\
"        uint64_t csr_cycle;"\
"    uint32_t csr_time[2];"\
"    uint32_t csr_mstatus;"\
"    uint32_t csr_mtvec;"\
"    uint32_t csr_misa;"\
"    uint32_t csr_mtval;"\
"    uint32_t csr_mcause;"\
"    uint32_t csr_mscratch;"\
"    uint32_t csr_mepc;"\
"    uint32_t csr_mip;"\
"    uint32_t csr_mbadaddr;"\
""\
"    bool compressed; "\
"        bool output_exit_code;"\
"};"\
"typedef struct {"\
"    uint8_t data[0x10000];"\
"} chunk_t;"\
""\
"typedef struct {"\
"    chunk_t *chunks[0x10000];"\
"} memory_t;"\
"typedef struct {"\
"    memory_t *mem;"\
""\
"        riscv_word_t break_addr;"\
""\
"        "\
"} state_t;"\
"typedef struct {"\
"    int32_t imm;"\
"    uint8_t rd, rs1, rs2;"\
"} opcode_fuse_t;"\
""\
"typedef struct rv_insn {"\
"    union {"\
"        int32_t imm;"\
"        uint8_t rs3;"\
"    };"\
"    uint8_t rd, rs1, rs2;"\
"        uint8_t opcode;"\
""\
"    uint8_t shamt;"\
"        int16_t imm2;"\
"    opcode_fuse_t *fuse;"\
""\
"        uint8_t insn_len;"\
""\
"        bool tailcall;"\
"    bool (*impl)(riscv_t *, const struct rv_insn *);"\
""\
"        struct rv_insn *branch_taken, *branch_untaken;"\
"    uint32_t pc;"\
"} rv_insn_t;"\
"bool start(volatile riscv_t *rv, rv_insn_t *ir) {"\
" uint32_t pc, addr, udividend, udivisor, tmp, data, mask, ures, "\
"a, b, jump_to;"\
"  int32_t dividend, divisor, res;"\
"  int64_t multiplicand, multiplier;"\
"  uint64_t umultiplier;"\
"  memory_t *m = ((state_t *)rv->userdata)->mem;"\
"  chunk_t *c;"
RVOP(nop, {
strcat(gencode, "/* no operation */\n");
})
RVOP(lui, {
GEN(" rv->X[%u] = %u; \n", ir->rd, ir->imm);
})
RVOP(auipc, {
GEN(" rv->X[%u] = %u + rv->PC; \n", ir->rd, ir->imm);
})
RVOP(jalr, {
strcat(gencode, "     pc = rv->PC;\n");
GEN("        rv->PC = (rv->X[%u] + %u) & ~1U;\n", ir->rs1, ir->imm);
GEN("        if (%u)\n", ir->rd);
GEN("        rv->X[%u] = pc + %u;\n", ir->rd, ir->insn_len);
strcat(gencode, "            return true;\n");
})
RVOP(addi, {
GEN(" rv->X[%u] = (int32_t) (rv->X[%u]) + %u; \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(slti, {
GEN(" rv->X[%u] = ((int32_t) (rv->X[%u]) < %u) ? 1 : 0; \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(sltiu, {
GEN(" rv->X[%u] = (rv->X[%u] < (uint32_t) %u) ? 1 : 0; \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(xori, {
GEN(" rv->X[%u] = rv->X[%u] ^ %u; \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(ori, {
GEN(" rv->X[%u] = rv->X[%u] | %u; \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(andi, {
GEN(" rv->X[%u] = rv->X[%u] & %u; \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(slli, {
GEN(" rv->X[%u] = rv->X[%u] << (%u & 0x1f); \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(srli, {
GEN(" rv->X[%u] = rv->X[%u] >> (%u & 0x1f); \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(srai, {
GEN(" rv->X[%u] = ((int32_t) rv->X[%u]) >> (%u & 0x1f); \n", ir->rd, ir->rs1, ir->imm);
})
RVOP(add, {
GEN("    rv->X[%u] = (int32_t) (rv->X[%u]) + (int32_t) (rv->X[%u]);\n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(sub, {
GEN("    rv->X[%u] = (int32_t) (rv->X[%u]) - (int32_t) (rv->X[%u]);\n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(sll, {
GEN(" rv->X[%u] = rv->X[%u] << (rv->X[%u] & 0x1f); \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(slt, {
GEN("    rv->X[%u] =\n", ir->rd);
GEN("        ((int32_t) (rv->X[%u]) < (int32_t) (rv->X[%u])) ? 1 : 0;\n", ir->rs1, ir->rs2);
})
RVOP(sltu, {
GEN(" rv->X[%u] = (rv->X[%u] < rv->X[%u]) ? 1 : 0; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(xor, {
GEN("  rv->X[%u] = rv->X[%u] ^ rv->X[%u];\n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(srl, {
GEN(" rv->X[%u] = rv->X[%u] >> (rv->X[%u] & 0x1f); \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(sra, {
GEN(" rv->X[%u] = ((int32_t) rv->X[%u]) >> (rv->X[%u] & 0x1f); \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(or, {
GEN(" rv->X[%u] = rv->X[%u] | rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(and, {
GEN(" rv->X[%u] = rv->X[%u] & rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(ecall, {
strcat(gencode, "    rv->compressed = false;\n");
strcat(gencode, "    rv->io.on_ecall(rv);\n");
strcat(gencode, "    return true;\n");
})
RVOP(ebreak, {
strcat(gencode, "    rv->compressed = false;\n");
strcat(gencode, "    rv->io.on_ebreak(rv);\n");
strcat(gencode, "    return true;\n");
})
RVOP(wfi, {
strcat(gencode, "        return false;\n");
})
RVOP(uret, {
strcat(gencode, "        return false;\n");
})
RVOP(sret, {
strcat(gencode, "        return false;\n");
})
RVOP(hret, {
strcat(gencode, "        return false;\n");
})
RVOP(mret, {
strcat(gencode, "    rv->PC = rv->csr_mepc;\n");
strcat(gencode, "    return true;\n");
})
RVOP(fencei, {
GEN("    rv->PC += %u;\n", ir->insn_len);
strcat(gencode, "        return true;\n");
})
RVOP(csrrw, {
GEN("    tmp = csr_csrrw(rv, %u, rv->X[%u]);\n", ir->imm, ir->rs1);
GEN("    rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
})
RVOP(csrrs, {
strcat(gencode, "    tmp =\n");
GEN("        csr_csrrs(rv, %u, (%u == rv_reg_zero) ? 0U : rv->X[%u]);\n", ir->imm, ir->rs1, ir->rs1);
GEN("    rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
})
RVOP(csrrc, {
strcat(gencode, "    tmp =\n");
GEN("        csr_csrrc(rv, %u, (%u == rv_reg_zero) ? ~0U : rv->X[%u]);\n", ir->imm, ir->rs1, ir->rs1);
GEN("    rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
})
RVOP(csrrwi, {
GEN("    tmp = csr_csrrw(rv, %u, %u);\n", ir->imm, ir->rs1);
GEN("    rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
})
RVOP(csrrsi, {
GEN("    tmp = csr_csrrs(rv, %u, %u);\n", ir->imm, ir->rs1);
GEN("    rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
})
RVOP(csrrci, {
GEN("    tmp = csr_csrrc(rv, %u, %u);\n", ir->imm, ir->rs1);
GEN("    rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
})
RVOP(mul, {
GEN(" rv->X[%u] = (int32_t) rv->X[%u] * (int32_t) rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(mulh, {
GEN("     multiplicand = (int32_t) rv->X[%u];\n", ir->rs1);
GEN("     multiplier = (int32_t) rv->X[%u];\n", ir->rs2);
GEN("    rv->X[%u] = ((uint64_t) (multiplicand * multiplier)) >> 32;\n", ir->rd);
})
RVOP(mulhsu, {
GEN("     multiplicand = (int32_t) rv->X[%u];\n", ir->rs1);
GEN("     umultiplier = rv->X[%u];\n", ir->rs2);
GEN("    rv->X[%u] = ((uint64_t) (multiplicand * umultiplier)) >> 32;\n", ir->rd);
})
RVOP(mulhu, {
GEN("    rv->X[%u] =\n", ir->rd);
GEN("        ((uint64_t) rv->X[%u] * (uint64_t) rv->X[%u]) >> 32;\n", ir->rs1, ir->rs2);
})
RVOP(div, {
GEN("     dividend = (int32_t) rv->X[%u];\n", ir->rs1);
GEN("     divisor = (int32_t) rv->X[%u];\n", ir->rs2);
GEN("    rv->X[%u] = !divisor ? ~0U\n", ir->rd);
GEN("                    : (divisor == -1 && rv->X[%u] == 0x80000000U)\n", ir->rs1);
GEN("                        ? rv->X[%u]                         : (unsigned int) (dividend / divisor);\n", ir->rs1);
})
RVOP(divu, {
GEN("     udividend = rv->X[%u];\n", ir->rs1);
GEN("     udivisor = rv->X[%u];\n", ir->rs2);
GEN("    rv->X[%u] = !udivisor ? ~0U : udividend / udivisor;\n", ir->rd);
})
RVOP(rem, {
GEN("     dividend = rv->X[%u];\n", ir->rs1);
GEN("     divisor = rv->X[%u];\n", ir->rs2);
GEN("    rv->X[%u] = !divisor ? dividend\n", ir->rd);
GEN("                    : (divisor == -1 && rv->X[%u] == 0x80000000U)\n", ir->rs1);
strcat(gencode, "                        ? 0                         : (dividend % divisor);\n");
})
RVOP(remu, {
GEN("     udividend = rv->X[%u];\n", ir->rs1);
GEN("     udivisor = rv->X[%u];\n", ir->rs2);
GEN("    rv->X[%u] = !udivisor ? udividend : udividend % udivisor;\n", ir->rd);
})
RVOP(lrw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, rv->X[%u]);\n", ir->rd, ir->rs1);
strcat(gencode, "    \n");
})
RVOP(scw, {
GEN("        rv->io.mem_write_w(rv, rv->X[%u], rv->X[%u]);\n", ir->rs1, ir->rs2);
GEN("    rv->X[%u] = 0;\n", ir->rd);
})
RVOP(amoswapw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
GEN("    rv->io.mem_write_s(rv, %u, rv->X[%u]);\n", ir->rs1, ir->rs2);
})
RVOP(amoaddw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
GEN("     res = (int32_t) rv->X[%u] + (int32_t) rv->X[%u];\n", ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
})
RVOP(amoxorw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
GEN("     res = rv->X[%u] ^ rv->X[%u];\n", ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
})
RVOP(amoandw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
GEN("     res = rv->X[%u] & rv->X[%u];\n", ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
})
RVOP(amoorw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
GEN("     res = rv->X[%u] | rv->X[%u];\n", ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
})
RVOP(amominw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
strcat(gencode, "     res =\n");
GEN("        rv->X[%u] < rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd, ir->rs2, ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
})
RVOP(amomaxw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
strcat(gencode, "     res =\n");
GEN("        rv->X[%u] > rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd, ir->rs2, ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, res);\n", ir->rs1);
})
RVOP(amominuw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
strcat(gencode, "     ures =\n");
GEN("        rv->X[%u] < rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd, ir->rs2, ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, ures);\n", ir->rs1);
})
RVOP(amomaxuw, {
GEN("    rv->X[%u] = rv->io.mem_read_w(rv, %u);\n", ir->rd, ir->rs1);
strcat(gencode, "     ures =\n");
GEN("        rv->X[%u] > rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd, ir->rs2, ir->rd, ir->rs2);
GEN("    rv->io.mem_write_s(rv, %u, ures);\n", ir->rs1);
})
RVOP(flw, {
GEN("         data = rv->io.mem_read_w(rv, rv->X[%u] + %u);\n", ir->rs1, ir->imm);
GEN("    rv->F_int[%u] = data;\n", ir->rd);
})
RVOP(fsw, {
GEN("        data = rv->F_int[%u];\n", ir->rs2);
GEN("    rv->io.mem_write_w(rv, rv->X[%u] + %u, data);\n", ir->rs1, ir->imm);
})
RVOP(fmadds, {
GEN(" rv->F[%u] = rv->F[%u] * rv->F[%u] + rv->F[%u]; \n", ir->rd, ir->rs1, ir->rs2, ir->rs3);
})
RVOP(fmsubs, {
GEN(" rv->F[%u] = rv->F[%u] * rv->F[%u] - rv->F[%u]; \n", ir->rd, ir->rs1, ir->rs2, ir->rs3);
})
RVOP(fnmsubs, {
GEN(" rv->F[%u] = rv->F[%u] - (rv->F[%u] * rv->F[%u]); \n", ir->rd, ir->rs3, ir->rs1, ir->rs2);
})
RVOP(fnmadds, {
GEN(" rv->F[%u] = -(rv->F[%u] * rv->F[%u]) - rv->F[%u]; \n", ir->rd, ir->rs1, ir->rs2, ir->rs3);
})
RVOP(fadds, {
GEN("    if (isnanf(rv->F[%u]) || isnanf(rv->F[%u]) ||\n", ir->rs1, ir->rs2);
GEN("        isnanf(rv->F[%u] + rv->F[%u])) {\n", ir->rs1, ir->rs2);
GEN("                rv->F_int[%u] = RV_NAN;         rv->csr_fcsr |= FFLAG_INVALID_OP;\n", ir->rd);
strcat(gencode, "    } else {\n");
GEN("        rv->F[%u] = rv->F[%u] + rv->F[%u];\n", ir->rd, ir->rs1, ir->rs2);
strcat(gencode, "    }\n");
GEN("    if (isinff(rv->F[%u])) {\n", ir->rd);
strcat(gencode, "        rv->csr_fcsr |= FFLAG_OVERFLOW;\n");
strcat(gencode, "        rv->csr_fcsr |= FFLAG_INEXACT;\n");
strcat(gencode, "    }\n");
})
RVOP(fsubs, {
GEN("    if (isnanf(rv->F[%u]) || isnanf(rv->F[%u])) {\n", ir->rs1, ir->rs2);
GEN("        rv->F_int[%u] = RV_NAN;\n", ir->rd);
strcat(gencode, "    } else {\n");
GEN("        rv->F[%u] = rv->F[%u] - rv->F[%u];\n", ir->rd, ir->rs1, ir->rs2);
strcat(gencode, "    }\n");
})
RVOP(fmuls, {
GEN(" rv->F[%u] = rv->F[%u] * rv->F[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(fdivs, {
GEN(" rv->F[%u] = rv->F[%u] / rv->F[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(fsqrts, {
GEN(" rv->F[%u] = sqrtf(rv->F[%u]); \n", ir->rd, ir->rs1);
})
RVOP(fsgnjs, {
GEN("     ures = (((uint32_t) rv->F_int[%u]) & ~FMASK_SIGN) |\n", ir->rs1);
GEN("                          (((uint32_t) rv->F_int[%u]) & FMASK_SIGN);\n", ir->rs2);
GEN("    rv->F_int[%u] = ures;\n", ir->rd);
})
RVOP(fsgnjns, {
GEN("     ures = (((uint32_t) rv->F_int[%u]) & ~FMASK_SIGN) |\n", ir->rs1);
GEN("                          (~((uint32_t) rv->F_int[%u]) & FMASK_SIGN);\n", ir->rs2);
GEN("    rv->F_int[%u] = ures;\n", ir->rd);
})
RVOP(fsgnjxs, {
GEN("     ures = ((uint32_t) rv->F_int[%u]) ^\n", ir->rs1);
GEN("                          (((uint32_t) rv->F_int[%u]) & FMASK_SIGN);\n", ir->rs2);
GEN("    rv->F_int[%u] = ures;\n", ir->rd);
})
RVOP(fmins, {
GEN("    a = rv->F_int[%u];\n", ir->rs1);
GEN("    b = rv->F_int[%u];\n", ir->rs2);
strcat(gencode, "    if (is_nan(a) || is_nan(b)) {\n");
strcat(gencode, "        if (is_snan(a) || is_snan(b))\n");
strcat(gencode, "            rv->csr_fcsr |= FFLAG_INVALID_OP;\n");
strcat(gencode, "        if (is_nan(a) && !is_nan(b)) {\n");
GEN("            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs2);
strcat(gencode, "        } else if (!is_nan(a) && is_nan(b)) {\n");
GEN("            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs1);
strcat(gencode, "        } else {\n");
GEN("            rv->F_int[%u] = RV_NAN;\n", ir->rd);
strcat(gencode, "        }\n");
strcat(gencode, "    } else {\n");
strcat(gencode, "        a_sign = a & FMASK_SIGN;\n");
strcat(gencode, "        b_sign = b & FMASK_SIGN;\n");
strcat(gencode, "        if (a_sign != b_sign) {\n");
GEN("            rv->F[%u] = a_sign ? rv->F[%u] : rv->F[%u];\n", ir->rd, ir->rs1, ir->rs2);
strcat(gencode, "        } else {\n");
GEN("            rv->F[%u] = (rv->F[%u] < rv->F[%u]) ? rv->F[%u]\n", ir->rd, ir->rs1, ir->rs2, ir->rs1);
GEN("                                                              : rv->F[%u];\n", ir->rs2);
strcat(gencode, "        }\n");
strcat(gencode, "    }\n");
})
RVOP(fmaxs, {
GEN("    a = rv->F_int[%u];\n", ir->rs1);
GEN("    b = rv->F_int[%u];\n", ir->rs2);
strcat(gencode, "    if (is_nan(a) || is_nan(b)) {\n");
strcat(gencode, "        if (is_snan(a) || is_snan(b))\n");
strcat(gencode, "            rv->csr_fcsr |= FFLAG_INVALID_OP;\n");
strcat(gencode, "        if (is_nan(a) && !is_nan(b)) {\n");
GEN("            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs2);
strcat(gencode, "        } else if (!is_nan(a) && is_nan(b)) {\n");
GEN("            rv->F[%u] = rv->F[%u];\n", ir->rd, ir->rs1);
strcat(gencode, "        } else {\n");
GEN("            rv->F_int[%u] = RV_NAN;\n", ir->rd);
strcat(gencode, "        }\n");
strcat(gencode, "    } else {\n");
strcat(gencode, "        a_sign = a & FMASK_SIGN;\n");
strcat(gencode, "        b_sign = b & FMASK_SIGN;\n");
strcat(gencode, "        if (a_sign != b_sign) {\n");
GEN("            rv->F[%u] = a_sign ? rv->F[%u] : rv->F[%u];\n", ir->rd, ir->rs2, ir->rs1);
strcat(gencode, "        } else {\n");
GEN("            rv->F[%u] = (rv->F[%u] > rv->F[%u]) ? rv->F[%u]\n", ir->rd, ir->rs1, ir->rs2, ir->rs1);
GEN("                                                              : rv->F[%u];\n", ir->rs2);
strcat(gencode, "        }\n");
strcat(gencode, "    }\n");
})
RVOP(fcvtws, {
GEN(" rv->X[%u] = (int32_t) rv->F[%u]; \n", ir->rd, ir->rs1);
})
RVOP(fcvtwus, {
GEN(" rv->X[%u] = (uint32_t) rv->F[%u]; \n", ir->rd, ir->rs1);
})
RVOP(fmvxw, {
GEN(" rv->X[%u] = rv->F_int[%u]; \n", ir->rd, ir->rs1);
})
RVOP(feqs, {
GEN("    rv->X[%u] = (rv->F[%u] == rv->F[%u]) ? 1 : 0;\n", ir->rd, ir->rs1, ir->rs2);
GEN("    if (is_snan(rv->F_int[%u]) || is_snan(rv->F_int[%u]))\n", ir->rs1, ir->rs2);
strcat(gencode, "        rv->csr_fcsr |= FFLAG_INVALID_OP;\n");
})
RVOP(flts, {
GEN("    rv->X[%u] = (rv->F[%u] < rv->F[%u]) ? 1 : 0;\n", ir->rd, ir->rs1, ir->rs2);
GEN("    if (is_nan(rv->F_int[%u]) || is_nan(rv->F_int[%u]))\n", ir->rs1, ir->rs2);
strcat(gencode, "        rv->csr_fcsr |= FFLAG_INVALID_OP;\n");
})
RVOP(fles, {
GEN("    rv->X[%u] = (rv->F[%u] <= rv->F[%u]) ? 1 : 0;\n", ir->rd, ir->rs1, ir->rs2);
GEN("    if (is_nan(rv->F_int[%u]) || is_nan(rv->F_int[%u]))\n", ir->rs1, ir->rs2);
strcat(gencode, "        rv->csr_fcsr |= FFLAG_INVALID_OP;\n");
})
RVOP(fclasss, {
GEN("    bits = rv->F_int[%u];\n", ir->rs1);
GEN("    rv->X[%u] = calc_fclass(bits);\n", ir->rd);
})
RVOP(fcvtsw, {
GEN(" rv->F[%u] = (int32_t) rv->X[%u]; \n", ir->rd, ir->rs1);
})
RVOP(fcvtswu, {
GEN(" rv->F[%u] = rv->X[%u]; \n", ir->rd, ir->rs1);
})
RVOP(fmvwx, {
GEN(" rv->F_int[%u] = rv->X[%u]; \n", ir->rd, ir->rs1);
})
RVOP(caddi4spn, {
GEN(" rv->X[%u] = rv->X[2] + (uint16_t) %u; \n", ir->rd, ir->imm);
})
RVOP(cnop, {
strcat(gencode, "/* no operation */\n");
})
RVOP(caddi, {
GEN(" rv->X[%u] += (int16_t) %u; \n", ir->rd, ir->imm);
})
RVOP(cli, {
GEN(" rv->X[%u] = %u; \n", ir->rd, ir->imm);
})
RVOP(caddi16sp, {
GEN(" rv->X[%u] += %u; \n", ir->rd, ir->imm);
})
RVOP(clui, {
GEN(" rv->X[%u] = %u; \n", ir->rd, ir->imm);
})
RVOP(csrli, {
GEN(" rv->X[%u] >>= %u; \n", ir->rs1, ir->shamt);
})
RVOP(csrai, {
GEN("     mask = 0x80000000 & rv->X[%u];\n", ir->rs1);
GEN("    rv->X[%u] >>= %u;\n", ir->rs1, ir->shamt);
GEN("    for (unsigned int i = 0; i < %u; ++i)\n", ir->shamt);
GEN("        rv->X[%u] |= mask >> i;\n", ir->rs1);
})
RVOP(candi, {
GEN(" rv->X[%u] &= %u; \n", ir->rs1, ir->imm);
})
RVOP(csub, {
GEN(" rv->X[%u] = rv->X[%u] - rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(cxor, {
GEN(" rv->X[%u] = rv->X[%u] ^ rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(cor, {
GEN(" rv->X[%u] = rv->X[%u] | rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(cand, {
GEN(" rv->X[%u] = rv->X[%u] & rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(cslli, {
GEN(" rv->X[%u] <<= (uint8_t) %u; \n", ir->rd, ir->imm);
})
RVOP(clwsp, {
GEN("     addr = rv->X[rv_reg_sp] + %u;\n", ir->imm);
GEN("        rv->X[%u] = rv->io.mem_read_w(rv, addr);\n", ir->rd);
})
RVOP(cjr, {
GEN("    rv->PC = rv->X[%u];\n", ir->rs1);
strcat(gencode, "    return true;\n");
})
RVOP(cmv, {
GEN(" rv->X[%u] = rv->X[%u]; \n", ir->rd, ir->rs2);
})
RVOP(cebreak, {
strcat(gencode, "    rv->compressed = true;\n");
strcat(gencode, "    rv->io.on_ebreak(rv);\n");
strcat(gencode, "    return true;\n");
})
RVOP(cjalr, {
GEN("         jump_to = rv->X[%u];\n", ir->rs1);
GEN("    rv->X[rv_reg_ra] = rv->PC + %u;\n", ir->insn_len);
strcat(gencode, "    rv->PC = jump_to;\n");
strcat(gencode, "        return true;\n");
})
RVOP(cadd, {
GEN(" rv->X[%u] = rv->X[%u] + rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(cswsp, {
GEN("     addr = rv->X[2] + %u;\n", ir->imm);
GEN("        rv->io.mem_write_w(rv, addr, rv->X[%u]);\n", ir->rs2);
})
RVOP(fuse1, {
GEN(" rv->X[%u] = (int32_t) (rv->PC + %u + %u); \n", ir->rd, ir->imm, ir->imm2);
})
RVOP(fuse2, {
GEN("    rv->X[%u] = (int32_t) (rv->X[%u]) + (int32_t) (rv->PC + %u);\n", ir->rd, ir->rs1, ir->imm);
})
