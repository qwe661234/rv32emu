#define PROLOGUE                                                               \
    "#include <stdint.h>\n"                                                    \
    "#include <stdbool.h>\n"                                                   \
    "typedef struct {"                                                         \
    "    uint8_t *mem_base;"                                                   \
    "    uint64_t mem_size;"                                                   \
    "} memory_t;"                                                              \
    "enum "                                                                    \
    "{rv_reg_zero,rv_reg_ra,rv_reg_sp,rv_reg_gp,rv_reg_tp,rv_reg_t0,rv_reg_"   \
    "t1,rv_reg_t2,rv_reg_s0,rv_reg_s1,rv_reg_a0,rv_reg_a1,rv_reg_a2,rv_reg_"   \
    "a3,rv_reg_a4,rv_reg_a5,rv_reg_a6,rv_reg_a7,rv_reg_s2,rv_reg_s3,rv_reg_"   \
    "s4,rv_reg_s5,rv_reg_s6,rv_reg_s7,rv_reg_s8,rv_reg_s9,rv_reg_s10,rv_reg_"  \
    "s11,rv_reg_t3,rv_reg_t4,rv_reg_t5,rv_reg_t6,N_RV_REGS };"                 \
    "typedef struct riscv_internal riscv_t;"                                   \
    "typedef void *riscv_user_t;"                                              \
    ""                                                                         \
    "typedef uint32_t riscv_word_t;"                                           \
    "typedef uint16_t riscv_half_t;"                                           \
    "typedef uint8_t riscv_byte_t;"                                            \
    "typedef uint32_t riscv_exception_t;"                                      \
    "typedef struct { uint32_t v; } float32_t;\
"                                \
    "typedef float32_t riscv_float_t;\
"                                         \
    "typedef riscv_word_t (*riscv_mem_ifetch)(riscv_word_t addr);"             \
    "typedef riscv_word_t (*riscv_mem_read_w)(riscv_word_t addr);"             \
    "typedef riscv_half_t (*riscv_mem_read_s)(riscv_word_t addr);"             \
    "typedef riscv_byte_t (*riscv_mem_read_b)(riscv_word_t addr);"             \
    ""                                                                         \
    ""                                                                         \
    "typedef void (*riscv_mem_write_w)(riscv_word_t addr, riscv_word_t data);" \
    "typedef void (*riscv_mem_write_s)(riscv_word_t addr, riscv_half_t data);" \
    "typedef void (*riscv_mem_write_b)(riscv_word_t addr, riscv_byte_t data);" \
    ""                                                                         \
    ""                                                                         \
    "typedef void (*riscv_on_ecall)(riscv_t *rv);"                             \
    "typedef void (*riscv_on_ebreak)(riscv_t *rv);"                            \
    "typedef void (*riscv_on_memset)(riscv_t *rv);"                            \
    "typedef void (*riscv_on_memcpy)(riscv_t *rv);"                            \
    ""                                                                         \
    "typedef struct {"                                                         \
    "    "                                                                     \
    "    riscv_mem_ifetch mem_ifetch;"                                         \
    "    riscv_mem_read_w mem_read_w;"                                         \
    "    riscv_mem_read_s mem_read_s;"                                         \
    "    riscv_mem_read_b mem_read_b;"                                         \
    ""                                                                         \
    "    "                                                                     \
    "    riscv_mem_write_w mem_write_w;"                                       \
    "    riscv_mem_write_s mem_write_s;"                                       \
    "    riscv_mem_write_b mem_write_b;"                                       \
    ""                                                                         \
    "    "                                                                     \
    "    riscv_on_ecall on_ecall;"                                             \
    "    riscv_on_ebreak on_ebreak;"                                           \
    "    riscv_on_memset on_memset;"                                           \
    "    riscv_on_memcpy on_memcpy;"                                           \
    "    "                                                                     \
    "    bool allow_misalign;"                                                 \
    "} riscv_io_t;"                                                            \
    "typedef struct {"                                                         \
    "    memory_t *mem;"                                                       \
    ""                                                                         \
    "    "                                                                     \
    "    riscv_word_t break_addr;"                                             \
    ""                                                                         \
    "    "                                                                     \
    "    "                                                                     \
    "} state_t;"                                                               \
    "enum {"                                                                   \
    "        FMASK_SIGN        = 0b10000000000000000000000000000000,"          \
    "    FMASK_EXPN        = 0b01111111100000000000000000000000,"              \
    "    FMASK_FRAC        = 0b00000000011111111111111111111111,"              \
    "    FMASK_QNAN        = 0b00000000010000000000000000000000,"              \
    "        FFLAG_MASK        = 0b00000000000000000000000000011111,"          \
    "    FFLAG_INVALID_OP  = 0b00000000000000000000000000010000,"              \
    "    FFLAG_DIV_BY_ZERO = 0b00000000000000000000000000001000,"              \
    "    FFLAG_OVERFLOW    = 0b00000000000000000000000000000100,"              \
    "    FFLAG_UNDERFLOW   = 0b00000000000000000000000000000010,"              \
    "    FFLAG_INEXACT     = 0b00000000000000000000000000000001,"              \
    "        RV_NAN            = 0b01111111110000000000000000000000"           \
    "};"                                                                       \
    "struct riscv_internal {"                                                  \
    "    bool halt; "                                                          \
    ""                                                                         \
    "    "                                                                     \
    "    riscv_io_t io;"                                                       \
    ""                                                                         \
    "    "                                                                     \
    "    riscv_word_t X[N_RV_REGS];"                                           \
    "    riscv_word_t PC;"                                                     \
    ""                                                                         \
    "    "                                                                     \
    "    riscv_user_t userdata;"                                               \
    ""                                                                         \
    ""                                                                         \
    "    "                                                                     \
    "    riscv_float_t F[N_RV_REGS];"                                          \
    "    uint32_t csr_fcsr;"                                                   \
    ""                                                                         \
    "    "                                                                     \
    "    uint64_t csr_cycle;    "                                              \
    "    uint32_t csr_time[2];  "                                              \
    "    uint32_t csr_mstatus;  "                                              \
    "    uint32_t csr_mtvec;    "                                              \
    "    uint32_t csr_misa;     "                                              \
    "    uint32_t csr_mtval;    "                                              \
    "    uint32_t csr_mcause;   "                                              \
    "    uint32_t csr_mscratch; "                                              \
    "    uint32_t csr_mepc;     "                                              \
    "    uint32_t csr_mip;      "                                              \
    "    uint32_t csr_mbadaddr;"                                               \
    ""                                                                         \
    "    bool compressed;};"                                                   \
    "static inline uint32_t sign_extend_h(const uint32_t x)"                   \
    "{"                                                                        \
    "    return (int32_t) ((int16_t) x);"                                      \
    "}"                                                                        \
    "static inline uint32_t sign_extend_b(const uint32_t x)"                   \
    "{"                                                                        \
    "    return (int32_t) ((int8_t) x);"                                       \
    "}"                                                                        \
    "bool start(riscv_t *rv, uint64_t cycle, uint32_t PC) {"                   \
    " uint32_t pc, addr, udividend, udivisor, tmp, data, mask, ures, "         \
    "a, b, jump_to;"                                                           \
    "  int32_t dividend, divisor, res;"                                        \
    "  int64_t multiplicand, multiplier;"                                      \
    "  uint64_t umultiplier;"                                                  \
    "  memory_t *m = ((state_t *)rv->userdata)->mem;"
RVOP(nop, { strcat(gencode, " rv->X[rv_reg_zero] = 0; \n"); })
RVOP(lui, { GEN(" rv->X[%u] = %u; \n", ir->rd, ir->imm); })
RVOP(auipc, { GEN(" rv->X[%u] = %u + PC; \n", ir->rd, ir->imm); })
RVOP(addi, {
    GEN(" rv->X[%u] = (int32_t) (rv->X[%u]) + %u; \n", ir->rd, ir->rs1,
        ir->imm);
})
RVOP(slti, {
    GEN(" rv->X[%u] = ((int32_t) (rv->X[%u]) < %u) ? 1 : 0; \n", ir->rd,
        ir->rs1, ir->imm);
})
RVOP(sltiu, {
    GEN(" rv->X[%u] = (rv->X[%u] < (uint32_t) %u) ? 1 : 0; \n", ir->rd, ir->rs1,
        ir->imm);
})
RVOP(xori,
     { GEN(" rv->X[%u] = rv->X[%u] ^ %u; \n", ir->rd, ir->rs1, ir->imm); })
RVOP(ori, { GEN(" rv->X[%u] = rv->X[%u] | %u; \n", ir->rd, ir->rs1, ir->imm); })
RVOP(andi,
     { GEN(" rv->X[%u] = rv->X[%u] & %u; \n", ir->rd, ir->rs1, ir->imm); })
RVOP(add, {
    GEN("        rv->X[%u] = (int32_t) (rv->X[%u]) + (int32_t) (rv->X[%u]);\n",
        ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(sub, {
    GEN("        rv->X[%u] = (int32_t) (rv->X[%u]) - (int32_t) (rv->X[%u]);\n",
        ir->rd, ir->rs1, ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(sll, {
    GEN(" rv->X[%u] = rv->X[%u] << (rv->X[%u] & 0x1f); \n", ir->rd, ir->rs1,
        ir->rs2);
})
RVOP(slt, {
    GEN("        rv->X[%u] =\n", ir->rd);
    GEN("            ((int32_t) (rv->X[%u]) < (int32_t) (rv->X[%u])) ? 1 : "
        "0;\n",
        ir->rs1, ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(sltu, {
    GEN(" rv->X[%u] = (rv->X[%u] < rv->X[%u]) ? 1 : 0; \n", ir->rd, ir->rs1,
        ir->rs2);
})
RVOP(xor, {
  GEN("      rv->X[%u] = rv->X[%u] ^ rv->X[%u];\n", ir->rd, ir->rs1, ir->rs2);
  strcat(gencode, "    \n");
})
RVOP(srl, {
    GEN(" rv->X[%u] = rv->X[%u] >> (rv->X[%u] & 0x1f); \n", ir->rd, ir->rs1,
        ir->rs2);
})
RVOP(sra, {
    GEN(" rv->X[%u] = ((int32_t) rv->X[%u]) >> (rv->X[%u] & 0x1f); \n", ir->rd,
        ir->rs1, ir->rs2);
})
RVOP(or, {
    GEN(" rv->X[%u] = rv->X[%u] | rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(and, {
    GEN(" rv->X[%u] = rv->X[%u] & rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
RVOP(ecall, {
    strcat(gencode, "        rv->compressed = false;\n");
    strcat(gencode, "        rv->csr_cycle = cycle;\n");
    strcat(gencode, "        rv->PC = PC;\n");
    strcat(gencode, "        rv->io.on_ecall(rv);\n");
    strcat(gencode, "        return true;\n");
    strcat(gencode, "    \n");
})
RVOP(ebreak, {
    strcat(gencode, "        rv->compressed = false;\n");
    strcat(gencode, "        rv->csr_cycle = cycle;\n");
    strcat(gencode, "        rv->PC = PC;\n");
    strcat(gencode, "        rv->io.on_ebreak(rv);\n");
    strcat(gencode, "        return true;\n");
    strcat(gencode, "    \n");
})
RVOP(wfi, {
    strcat(gencode, "        \n");
    strcat(gencode, "        return false;\n");
    strcat(gencode, "    \n");
})
RVOP(uret, {
    strcat(gencode, "        \n");
    strcat(gencode, "        return false;\n");
    strcat(gencode, "    \n");
})
RVOP(sret, {
    strcat(gencode, "        \n");
    strcat(gencode, "        return false;\n");
    strcat(gencode, "    \n");
})
RVOP(hret, {
    strcat(gencode, "        \n");
    strcat(gencode, "        return false;\n");
    strcat(gencode, "    \n");
})
RVOP(mret, {
    strcat(gencode, "        rv->csr_mstatus = MSTATUS_MPIE;\n");
    strcat(gencode, "        rv->PC = rv->csr_mepc;\n");
    strcat(gencode, "        return true;\n");
    strcat(gencode, "    \n");
})
RVOP(fencei, {
    strcat(gencode, "        PC += 4;\n");
    strcat(gencode, "        \n");
    strcat(gencode, "        rv->csr_cycle = cycle;\n");
    strcat(gencode, "        rv->PC = PC;\n");
    strcat(gencode, "        return true;\n");
    strcat(gencode, "    \n");
})
RVOP(csrrw, {
    GEN("        tmp = csr_csrrw(rv, %u, rv->X[%u]);\n", ir->imm, ir->rs1);
    GEN("        rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
    strcat(gencode, "    \n");
})
RVOP(csrrs, {
    strcat(gencode, "        tmp = csr_csrrs(\n");
    GEN("            rv, %u, (%u == rv_reg_zero) ? 0U : rv->X[%u]);\n", ir->imm,
        ir->rs1, ir->rs1);
    GEN("        rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
    strcat(gencode, "    \n");
})
RVOP(csrrc, {
    strcat(gencode, "        tmp = csr_csrrc(\n");
    GEN("            rv, %u, (%u == rv_reg_zero) ? ~0U : rv->X[%u]);\n",
        ir->imm, ir->rs1, ir->rs1);
    GEN("        rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
    strcat(gencode, "    \n");
})
RVOP(csrrwi, {
    GEN("        tmp = csr_csrrw(rv, %u, %u);\n", ir->imm, ir->rs1);
    GEN("        rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
    strcat(gencode, "    \n");
})
RVOP(csrrsi, {
    GEN("        tmp = csr_csrrs(rv, %u, %u);\n", ir->imm, ir->rs1);
    GEN("        rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
    strcat(gencode, "    \n");
})
RVOP(csrrci, {
    GEN("        tmp = csr_csrrc(rv, %u, %u);\n", ir->imm, ir->rs1);
    GEN("        rv->X[%u] = %u ? tmp : rv->X[%u];\n", ir->rd, ir->rd, ir->rd);
    strcat(gencode, "    \n");
})
RVOP(mul, {
    GEN(" rv->X[%u] = (int32_t) rv->X[%u] * (int32_t) rv->X[%u]; \n", ir->rd,
        ir->rs1, ir->rs2);
})
RVOP(mulh, {
    GEN("         multiplicand = (int32_t) rv->X[%u];\n", ir->rs1);
    GEN("         multiplier = (int32_t) rv->X[%u];\n", ir->rs2);
    GEN("        rv->X[%u] = ((uint64_t) (multiplicand * multiplier)) >> 32;\n",
        ir->rd);
    strcat(gencode, "    \n");
})
RVOP(mulhsu, {
    GEN("         multiplicand = (int32_t) rv->X[%u];\n", ir->rs1);
    GEN("         umultiplier = rv->X[%u];\n", ir->rs2);
    GEN("        rv->X[%u] = ((uint64_t) (multiplicand * umultiplier)) >> "
        "32;\n",
        ir->rd);
    strcat(gencode, "    \n");
})
RVOP(mulhu, {
    GEN("        rv->X[%u] =\n", ir->rd);
    GEN("            ((uint64_t) rv->X[%u] * (uint64_t) rv->X[%u]) >> 32;\n",
        ir->rs1, ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(div, {
    GEN("         dividend = (int32_t) rv->X[%u];\n", ir->rs1);
    GEN("         divisor = (int32_t) rv->X[%u];\n", ir->rs2);
    GEN("        rv->X[%u] = !divisor ? ~0U\n", ir->rd);
    GEN("                        : (divisor == -1 && rv->X[%u] == "
        "0x80000000U)\n",
        ir->rs1);
    GEN("                            ? rv->X[%u] \n", ir->rs1);
    strcat(
        gencode,
        "                            : (unsigned int) (dividend / divisor);\n");
    strcat(gencode, "    \n");
})
RVOP(divu, {
    GEN("         udividend = rv->X[%u];\n", ir->rs1);
    GEN("         udivisor = rv->X[%u];\n", ir->rs2);
    GEN("        rv->X[%u] = !udivisor ? ~0U : udividend / udivisor;\n",
        ir->rd);
    strcat(gencode, "    \n");
})
RVOP(rem, {
    GEN("     dividend = rv->X[%u];\n", ir->rs1);
    GEN("     divisor = rv->X[%u];\n", ir->rs2);
    GEN("    rv->X[%u] = !divisor ? dividend\n", ir->rd);
    GEN("                    : (divisor == -1 && rv->X[%u] == 0x80000000U)\n",
        ir->rs1);
    strcat(gencode, "                        ? 0  : (dividend \n");
    strcat(gencode, "                        % divisor);\n");
})
RVOP(remu, {
    GEN("     udividend = rv->X[%u];\n", ir->rs1);
    GEN("     udivisor = rv->X[%u];\n", ir->rs2);
    GEN("    rv->X[%u] = !udivisor ? udividend : udividend \n", ir->rd);
    strcat(gencode, "    % udivisor;\n");
})
RVOP(lrw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(rv->X[%u]);\n", ir->rd, ir->rs1);
    strcat(gencode, "        \n");
    strcat(gencode, "    \n");
})
RVOP(scw, {
    strcat(gencode, "        \n");
    GEN("        rv->io.mem_write_w(rv->X[%u], rv->X[%u]);\n", ir->rs1,
        ir->rs2);
    GEN("        rv->X[%u] = 0;\n", ir->rd);
    strcat(gencode, "    \n");
})
RVOP(amoswapw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    GEN("        rv->io.mem_write_s(%u, rv->X[%u]);\n", ir->rs1, ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(amoaddw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    GEN("         res = (int32_t) rv->X[%u] + (int32_t) rv->X[%u];\n", ir->rd,
        ir->rs2);
    GEN("        rv->io.mem_write_s(%u, res);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(amoxorw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    GEN("         res = rv->X[%u] ^ rv->X[%u];\n", ir->rd, ir->rs2);
    GEN("        rv->io.mem_write_s(%u, res);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(amoandw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    GEN("         res = rv->X[%u] & rv->X[%u];\n", ir->rd, ir->rs2);
    GEN("        rv->io.mem_write_s(%u, res);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(amoorw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    GEN("         res = rv->X[%u] | rv->X[%u];\n", ir->rd, ir->rs2);
    GEN("        rv->io.mem_write_s(%u, res);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(amominw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    strcat(gencode, "         res =\n");
    GEN("            rv->X[%u] < rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd,
        ir->rs2, ir->rd, ir->rs2);
    GEN("        rv->io.mem_write_s(%u, res);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(amomaxw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    strcat(gencode, "         res =\n");
    GEN("            rv->X[%u] > rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd,
        ir->rs2, ir->rd, ir->rs2);
    GEN("        rv->io.mem_write_s(%u, res);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(amominuw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    strcat(gencode, "         ures =\n");
    GEN("            rv->X[%u] < rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd,
        ir->rs2, ir->rd, ir->rs2);
    GEN("        rv->io.mem_write_s(%u, ures);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(amomaxuw, {
    GEN("        rv->X[%u] = rv->io.mem_read_w(%u);\n", ir->rd, ir->rs1);
    strcat(gencode, "         ures =\n");
    GEN("            rv->X[%u] > rv->X[%u] ? rv->X[%u] : rv->X[%u];\n", ir->rd,
        ir->rs2, ir->rd, ir->rs2);
    GEN("        rv->io.mem_write_s(%u, ures);\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(fmadds, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] =\n", ir->rd);
    GEN("            f32_mulAdd(rv->F[%u], rv->F[%u], rv->F[%u]);\n", ir->rs1,
        ir->rs2, ir->rs3);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fmsubs, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        riscv_float_t tmp = rv->F[%u];\n", ir->rs3);
    strcat(gencode, "        tmp.v ^= FMASK_SIGN;\n");
    GEN("        rv->F[%u] = f32_mulAdd(rv->F[%u], rv->F[%u], tmp);\n", ir->rd,
        ir->rs1, ir->rs2);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fnmsubs, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        riscv_float_t tmp = rv->F[%u];\n", ir->rs1);
    strcat(gencode, "        tmp.v ^= FMASK_SIGN;\n");
    GEN("        rv->F[%u] = f32_mulAdd(tmp, rv->F[%u], rv->F[%u]);\n", ir->rd,
        ir->rs2, ir->rs3);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fnmadds, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        riscv_float_t tmp1 = rv->F[%u];\n", ir->rs1);
    GEN("        riscv_float_t tmp2 = rv->F[%u];\n", ir->rs3);
    strcat(gencode, "        tmp1.v ^= FMASK_SIGN;\n");
    strcat(gencode, "        tmp2.v ^= FMASK_SIGN;\n");
    GEN("        rv->F[%u] = f32_mulAdd(tmp1, rv->F[%u], tmp2);\n", ir->rd,
        ir->rs2);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fadds, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] = f32_add(rv->F[%u], rv->F[%u]);\n", ir->rd, ir->rs1,
        ir->rs2);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fsubs, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] = f32_sub(rv->F[%u], rv->F[%u]);\n", ir->rd, ir->rs1,
        ir->rs2);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fmuls, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] = f32_mul(rv->F[%u], rv->F[%u]);\n", ir->rd, ir->rs1,
        ir->rs2);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fdivs, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] = f32_div(rv->F[%u], rv->F[%u]);\n", ir->rd, ir->rs1,
        ir->rs2);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fsqrts, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] = f32_sqrt(rv->F[%u]);\n", ir->rd, ir->rs1);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fsgnjs, {
    GEN("        rv->F[%u].v =\n", ir->rd);
    GEN("            (rv->F[%u].v & ~FMASK_SIGN) | (rv->F[%u].v & "
        "FMASK_SIGN);\n",
        ir->rs1, ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(fsgnjns, {
    GEN("        rv->F[%u].v =\n", ir->rd);
    GEN("            (rv->F[%u].v & ~FMASK_SIGN) | (~rv->F[%u].v & "
        "FMASK_SIGN);\n",
        ir->rs1, ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(fsgnjxs, {
    GEN(" rv->F[%u].v = rv->F[%u].v ^ (rv->F[%u].v & FMASK_SIGN); \n", ir->rd,
        ir->rs1, ir->rs2);
})
RVOP(fmins, {
    GEN("        if (f32_isSignalingNaN(rv->F[%u]) ||\n", ir->rs1);
    GEN("            f32_isSignalingNaN(rv->F[%u]))\n", ir->rs2);
    strcat(gencode, "            rv->csr_fcsr |= FFLAG_INVALID_OP;\n");
    GEN("        bool less = f32_lt_quiet(rv->F[%u], rv->F[%u]) ||\n", ir->rs1,
        ir->rs2);
    GEN("                    (f32_eq(rv->F[%u], rv->F[%u]) &&\n", ir->rs1,
        ir->rs2);
    GEN("                     (rv->F[%u].v & FMASK_SIGN));\n", ir->rs1);
    GEN("        if (is_nan(rv->F[%u].v) && is_nan(rv->F[%u].v))\n", ir->rs1,
        ir->rs2);
    GEN("            rv->F[%u].v = RV_NAN;\n", ir->rd);
    strcat(gencode, "        else\n");
    GEN("            rv->F[%u] = (less || is_nan(rv->F[%u].v) ? rv->F[%u]\n",
        ir->rd, ir->rs2, ir->rs1);
    GEN("                                                              : "
        "rv->F[%u]);\n",
        ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(fmaxs, {
    GEN("        if (f32_isSignalingNaN(rv->F[%u]) ||\n", ir->rs1);
    GEN("            f32_isSignalingNaN(rv->F[%u]))\n", ir->rs2);
    strcat(gencode, "            rv->csr_fcsr |= FFLAG_INVALID_OP;\n");
    GEN("        bool greater = f32_lt_quiet(rv->F[%u], rv->F[%u]) ||\n",
        ir->rs2, ir->rs1);
    GEN("                       (f32_eq(rv->F[%u], rv->F[%u]) &&\n", ir->rs1,
        ir->rs2);
    GEN("                        (rv->F[%u].v & FMASK_SIGN));\n", ir->rs2);
    GEN("        if (is_nan(rv->F[%u].v) && is_nan(rv->F[%u].v))\n", ir->rs1,
        ir->rs2);
    GEN("            rv->F[%u].v = RV_NAN;\n", ir->rd);
    strcat(gencode, "        else\n");
    GEN("            rv->F[%u] =\n", ir->rd);
    GEN("                (greater || is_nan(rv->F[%u].v) ? rv->F[%u]\n",
        ir->rs2, ir->rs1);
    GEN("                                                     : rv->F[%u]);\n",
        ir->rs2);
    strcat(gencode, "    \n");
})
RVOP(fcvtws, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        uint32_t ret = f32_to_i32(rv->F[%u], softfloat_roundingMode, "
        "true);\n",
        ir->rs1);
    GEN("        if (%u)\n", ir->rd);
    GEN("            rv->X[%u] = ret;\n", ir->rd);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fcvtwus, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    strcat(gencode, "        uint32_t ret =\n");
    GEN("            f32_to_ui32(rv->F[%u], softfloat_roundingMode, true);\n",
        ir->rs1);
    GEN("        if (%u)\n", ir->rd);
    GEN("            rv->X[%u] = ret;\n", ir->rd);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fmvxw, {
    GEN("        if (%u)\n", ir->rd);
    GEN("            rv->X[%u] = rv->F[%u].v;\n", ir->rd, ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(feqs, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        uint32_t ret = f32_eq(rv->F[%u], rv->F[%u]);\n", ir->rs1,
        ir->rs2);
    GEN("        if (%u)\n", ir->rd);
    GEN("            rv->X[%u] = ret;\n", ir->rd);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(flts, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        uint32_t ret = f32_lt(rv->F[%u], rv->F[%u]);\n", ir->rs1,
        ir->rs2);
    GEN("        if (%u)\n", ir->rd);
    GEN("            rv->X[%u] = ret;\n", ir->rd);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fles, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        uint32_t ret = f32_le(rv->F[%u], rv->F[%u]);\n", ir->rs1,
        ir->rs2);
    GEN("        if (%u)\n", ir->rd);
    GEN("            rv->X[%u] = ret;\n", ir->rd);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fclasss, {
    GEN("        if (%u)\n", ir->rd);
    GEN("            rv->X[%u] = calc_fclass(rv->F[%u].v);\n", ir->rd, ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(fcvtsw, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] = i32_to_f32(rv->X[%u]);\n", ir->rd, ir->rs1);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fcvtswu, {
    strcat(gencode, "        set_rounding_mode(rv);\n");
    GEN("        rv->F[%u] = ui32_to_f32(rv->X[%u]);\n", ir->rd, ir->rs1);
    strcat(gencode, "        set_fflag(rv);\n");
    strcat(gencode, "    \n");
})
RVOP(fmvwx, { GEN(" rv->F[%u].v = rv->X[%u]; \n", ir->rd, ir->rs1); })
RVOP(caddi4spn, {
    GEN(" rv->X[%u] = rv->X[rv_reg_sp] + (uint16_t) %u; \n", ir->rd, ir->imm);
})
RVOP(cnop, {})
RVOP(caddi, { GEN(" rv->X[%u] += (int16_t) %u; \n", ir->rd, ir->imm); })
RVOP(cli, { GEN(" rv->X[%u] = %u; \n", ir->rd, ir->imm); })
RVOP(caddi16sp, { GEN(" rv->X[%u] += %u; \n", ir->rd, ir->imm); })
RVOP(clui, { GEN(" rv->X[%u] = %u; \n", ir->rd, ir->imm); })
RVOP(csrli, { GEN(" rv->X[%u] >>= %u; \n", ir->rs1, ir->shamt); })
RVOP(csrai, {
    GEN("         mask = 0x80000000 & rv->X[%u];\n", ir->rs1);
    GEN("        rv->X[%u] >>= %u;\n", ir->rs1, ir->shamt);
    GEN("        for (unsigned int i = 0; i < %u; ++i)\n", ir->shamt);
    GEN("            rv->X[%u] |= mask >> i;\n", ir->rs1);
    strcat(gencode, "    \n");
})
RVOP(candi, { GEN(" rv->X[%u] &= %u; \n", ir->rs1, ir->imm); })
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
RVOP(cslli, { GEN(" rv->X[%u] <<= (uint8_t) %u; \n", ir->rd, ir->imm); })
RVOP(cmv, { GEN(" rv->X[%u] = rv->X[%u]; \n", ir->rd, ir->rs2); })
RVOP(cebreak, {
    strcat(gencode, "        rv->compressed = true;\n");
    strcat(gencode, "        rv->csr_cycle = cycle;\n");
    strcat(gencode, "        rv->PC = PC;\n");
    strcat(gencode, "        rv->io.on_ebreak(rv);\n");
    strcat(gencode, "        return true;\n");
    strcat(gencode, "    \n");
})
RVOP(cadd, {
    GEN(" rv->X[%u] = rv->X[%u] + rv->X[%u]; \n", ir->rd, ir->rs1, ir->rs2);
})
