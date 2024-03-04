#include <assert.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "decode.h"
#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"

struct LLVM_block_map_entry {
    uint32_t pc;
    LLVMBasicBlockRef block;
};

struct LLVM_block_map {
    uint32_t count;
    struct LLVM_block_map_entry map[1024];
};

typedef intptr_t (*funcPtr_t)(riscv_t *);

void nop(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    return;
}

void lui(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   addr_rd);
}

void auipc(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMBuildStore(*builder,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_rd);
}

void jal(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    if (ir->rd) {
        LLVMValueRef rd_offset[1] = {
            LLVMConstInt(LLVMInt32Type(),
                         offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),
                                                     LLVMGetParam(start, 0),
                                                     rd_offset, 1, "addr_rd");
        LLVMBuildStore(
            *builder, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_rd);
    }
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    if (ir->branch_taken) {
        *taken_builder = *builder;
    } else {
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(*builder);
    }
}

void jalr(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    if (ir->rd) {
        LLVMValueRef rd_offset[1] = {
            LLVMConstInt(LLVMInt32Type(),
                         offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),
                                                     LLVMGetParam(start, 0),
                                                     rd_offset, 1, "addr_rd");
        LLVMBuildStore(
            *builder, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_rd);
    }
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res1 = LLVMBuildAdd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "add");
    LLVMValueRef res2 = LLVMBuildAnd(
        *builder, res1, LLVMConstInt(LLVMInt32Type(), ~1U, true), "and");
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, res2, addr_PC);
    LLVMBuildRetVoid(*builder);
}

void beq(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntEQ, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bne(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntNE, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void blt(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntSLT, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bge(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntSGE, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bltu(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntULT, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bgeu(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntUGE, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void lb(LLVMBuilderRef *builder UNUSED,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start UNUSED,
        LLVMBasicBlockRef *entry UNUSED,
        LLVMBuilderRef *taken_builder UNUSED,
        LLVMBuilderRef *untaken_builder UNUSED,
        uint64_t mem_base UNUSED,
        rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt8Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildSExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt8Type(), cast_addr, "res"),
        LLVMInt32Type(), "sext8to32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void lh(LLVMBuilderRef *builder UNUSED,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start UNUSED,
        LLVMBasicBlockRef *entry UNUSED,
        LLVMBuilderRef *taken_builder UNUSED,
        LLVMBuilderRef *untaken_builder UNUSED,
        uint64_t mem_base UNUSED,
        rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt16Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildSExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt16Type(), cast_addr, "res"),
        LLVMInt32Type(), "sext16to32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void lw(LLVMBuilderRef *builder UNUSED,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start UNUSED,
        LLVMBasicBlockRef *entry UNUSED,
        LLVMBuilderRef *taken_builder UNUSED,
        LLVMBuilderRef *untaken_builder UNUSED,
        uint64_t mem_base UNUSED,
        rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
    LLVMBuildStore(*builder, res, addr_rd);
}

void lbu(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt8Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt8Type(), cast_addr, "res"),
        LLVMInt32Type(), "zext8to32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void lhu(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt16Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt16Type(), cast_addr, "res"),
        LLVMInt32Type(), "zext16to32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void sb(LLVMBuilderRef *builder UNUSED,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start UNUSED,
        LLVMBasicBlockRef *entry UNUSED,
        LLVMBuilderRef *taken_builder UNUSED,
        LLVMBuilderRef *untaken_builder UNUSED,
        uint64_t mem_base UNUSED,
        rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt8Type(), addr_rs2, "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt8Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
}

void sh(LLVMBuilderRef *builder UNUSED,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start UNUSED,
        LLVMBasicBlockRef *entry UNUSED,
        LLVMBuilderRef *taken_builder UNUSED,
        LLVMBuilderRef *untaken_builder UNUSED,
        uint64_t mem_base UNUSED,
        rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt16Type(), addr_rs2, "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt16Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
}

void sw(LLVMBuilderRef *builder UNUSED,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start UNUSED,
        LLVMBasicBlockRef *entry UNUSED,
        LLVMBuilderRef *taken_builder UNUSED,
        LLVMBuilderRef *untaken_builder UNUSED,
        uint64_t mem_base UNUSED,
        rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
}

void addi(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "add");
    LLVMBuildStore(*builder, res, addr_rd);
}

void slti(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntSLT, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), ir->imm, false), "cond");
    LLVMBasicBlockRef new_entry = LLVMAppendBasicBlock(start, "new_entry");
    LLVMBuilderRef new_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(new_builder, new_entry);
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    LLVMBuildStore(builder2, LLVMConstInt(LLVMInt32Type(), 1, true), addr_rd);
    LLVMBuildBr(builder2, new_entry);
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), 0, true), addr_rd);
    LLVMBuildBr(builder3, new_entry);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
    *entry = new_entry;
    *builder = new_builder;
}

void sltiu(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntULT, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), ir->imm, false), "cond");
    LLVMBasicBlockRef new_entry = LLVMAppendBasicBlock(start, "new_entry");
    LLVMBuilderRef new_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(new_builder, new_entry);
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    LLVMBuildStore(builder2, LLVMConstInt(LLVMInt32Type(), 1, true), addr_rd);
    LLVMBuildBr(builder2, new_entry);
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), 0, true), addr_rd);
    LLVMBuildBr(builder3, new_entry);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
    *entry = new_entry;
    *builder = new_builder;
}

void xori(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildXor(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "xor");
    LLVMBuildStore(*builder, res, addr_rd);
}

void ori(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildOr(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "or");
    LLVMBuildStore(*builder, res, addr_rd);
}

void andi(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildAnd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "and");
    LLVMBuildStore(*builder, res, addr_rd);
}

void slli(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildShl(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm & 0x1f, true),
        "sll");
    LLVMBuildStore(*builder, res, addr_rd);
}

void srli(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildLShr(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm & 0x1f, true),
        "srl");
    LLVMBuildStore(*builder, res, addr_rd);
}

void srai(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildAShr(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm & 0x1f, true),
        "sra");
    LLVMBuildStore(*builder, res, addr_rd);
}

void add(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, addr_rd);
}

void sub(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildSub(*builder, val_rs1, val_rs2, "sub");
    LLVMBuildStore(*builder, res, addr_rd);
}

void sll(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef tmp =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef val_rs2 = LLVMBuildAnd(
        *builder, tmp, LLVMConstInt(LLVMInt32Type(), 0x1f, true), "and");
    LLVMValueRef res = LLVMBuildShl(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, addr_rd);
}

void slt(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntSLT, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef new_entry = LLVMAppendBasicBlock(start, "new_entry");
    LLVMBuilderRef new_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(new_builder, new_entry);
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    LLVMBuildStore(builder2, LLVMConstInt(LLVMInt32Type(), 1, true), addr_rd);
    LLVMBuildBr(builder2, new_entry);
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), 0, true), addr_rd);
    LLVMBuildBr(builder3, new_entry);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
    *entry = new_entry;
    *builder = new_builder;
}

void sltu(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntULT, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef new_entry = LLVMAppendBasicBlock(start, "new_entry");
    LLVMBuilderRef new_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(new_builder, new_entry);
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    LLVMBuildStore(builder2, LLVMConstInt(LLVMInt32Type(), 1, true), addr_rd);
    LLVMBuildBr(builder2, new_entry);
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), 0, true), addr_rd);
    LLVMBuildBr(builder3, new_entry);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
    *entry = new_entry;
    *builder = new_builder;
}

void xor
    (LLVMBuilderRef * builder UNUSED,
     LLVMTypeRef *param_types UNUSED,
     LLVMValueRef start UNUSED,
     LLVMBasicBlockRef *entry UNUSED,
     LLVMBuilderRef *taken_builder UNUSED,
     LLVMBuilderRef *untaken_builder UNUSED,
     uint64_t mem_base UNUSED,
     rv_insn_t *ir UNUSED) {
        LLVMValueRef rs1_offset[1] = {
            LLVMConstInt(LLVMInt32Type(),
                         offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
        LLVMValueRef addr_rs1 = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32Type(), LLVMGetParam(start, 0), rs1_offset, 1,
            "addr_rs1");
        LLVMValueRef rs2_offset[1] = {
            LLVMConstInt(LLVMInt32Type(),
                         offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
        LLVMValueRef addr_rs2 = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32Type(), LLVMGetParam(start, 0), rs2_offset, 1,
            "addr_rs2");
        LLVMValueRef rd_offset[1] = {
            LLVMConstInt(LLVMInt32Type(),
                         offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),
                                                     LLVMGetParam(start, 0),
                                                     rd_offset, 1, "addr_rd");
        LLVMValueRef val_rs1 =
            LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
        LLVMValueRef val_rs2 =
            LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
        LLVMValueRef res = LLVMBuildXor(*builder, val_rs1, val_rs2, "xor");
        LLVMBuildStore(*builder, res, addr_rd);
    }

    void srl(LLVMBuilderRef *builder UNUSED,
             LLVMTypeRef *param_types UNUSED,
             LLVMValueRef start UNUSED,
             LLVMBasicBlockRef *entry UNUSED,
             LLVMBuilderRef *taken_builder UNUSED,
             LLVMBuilderRef *untaken_builder UNUSED,
             uint64_t mem_base UNUSED,
             rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef tmp =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef val_rs2 = LLVMBuildAnd(
        *builder, tmp, LLVMConstInt(LLVMInt32Type(), 0x1f, true), "and");
    LLVMValueRef res = LLVMBuildLShr(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, addr_rd);
}

void sra(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef tmp =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef val_rs2 = LLVMBuildAnd(
        *builder, tmp, LLVMConstInt(LLVMInt32Type(), 0x1f, true), "and");
    LLVMValueRef res = LLVMBuildAShr(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, addr_rd);
}

void or (LLVMBuilderRef * builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildOr(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, addr_rd);
}

void and (LLVMBuilderRef * builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildAnd(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, addr_rd);
}

void ecall(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   addr_PC);
    LLVMValueRef ecall_offset[1] = {LLVMConstInt(LLVMInt32Type(), 8, true)};
    LLVMValueRef addr_io = LLVMBuildInBoundsGEP2(
        *builder, LLVMPointerType(LLVMVoidType(), 0), LLVMGetParam(start, 0),
        ecall_offset, 1, "addr_rv");
    LLVMValueRef func_ecall = LLVMBuildLoad2(
        *builder,
        LLVMPointerType(LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), 0),
        addr_io, "func_ecall");
    LLVMValueRef ecall_param[1] = {LLVMGetParam(start, 0)};
    LLVMBuildCall2(*builder,
                   LLVMFunctionType(LLVMVoidType(), param_types, 1, 0),
                   func_ecall, ecall_param, 1, "");
    LLVMBuildRetVoid(*builder);
}

void ebreak(LLVMBuilderRef *builder UNUSED,
            LLVMTypeRef *param_types UNUSED,
            LLVMValueRef start UNUSED,
            LLVMBasicBlockRef *entry UNUSED,
            LLVMBuilderRef *taken_builder UNUSED,
            LLVMBuilderRef *untaken_builder UNUSED,
            uint64_t mem_base UNUSED,
            rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   addr_PC);
    LLVMValueRef ebreak_offset[1] = {LLVMConstInt(LLVMInt32Type(), 9, true)};
    LLVMValueRef addr_io = LLVMBuildInBoundsGEP2(
        *builder, LLVMPointerType(LLVMVoidType(), 0), LLVMGetParam(start, 0),
        ebreak_offset, 1, "addr_rv");
    LLVMValueRef func_ebreak = LLVMBuildLoad2(
        *builder,
        LLVMPointerType(LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), 0),
        addr_io, "func_ebreak");
    LLVMValueRef ebreak_param[1] = {LLVMGetParam(start, 0)};
    LLVMBuildCall2(*builder,
                   LLVMFunctionType(LLVMVoidType(), param_types, 1, 0),
                   func_ebreak, ebreak_param, 1, "");
    LLVMBuildRetVoid(*builder);
}

void mul(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "sextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2"),
        LLVMInt64Type(), "sextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildAnd(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                     LLVMConstInt(LLVMInt64Type(), 0xFFFFFFFF, false), "and");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void mulh(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "sextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2"),
        LLVMInt64Type(), "sextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildLShr(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                      LLVMConstInt(LLVMInt64Type(), 32, false), "sll");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void mulhsu(LLVMBuilderRef *builder UNUSED,
            LLVMTypeRef *param_types UNUSED,
            LLVMValueRef start UNUSED,
            LLVMBasicBlockRef *entry UNUSED,
            LLVMBuilderRef *taken_builder UNUSED,
            LLVMBuilderRef *untaken_builder UNUSED,
            uint64_t mem_base UNUSED,
            rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "sextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2"),
        LLVMInt64Type(), "zextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildLShr(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                      LLVMConstInt(LLVMInt64Type(), 32, false), "sll");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void mulhu(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2"),
        LLVMInt64Type(), "zextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildLShr(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                      LLVMConstInt(LLVMInt64Type(), 32, false), "sll");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, addr_rd);
}

void divaaa(LLVMBuilderRef *builder UNUSED,
            LLVMTypeRef *param_types UNUSED,
            LLVMValueRef start UNUSED,
            LLVMBasicBlockRef *entry UNUSED,
            LLVMBuilderRef *taken_builder UNUSED,
            LLVMBuilderRef *untaken_builder UNUSED,
            uint64_t mem_base UNUSED,
            rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildSDiv(*builder, val_rs1, val_rs2, "sdiv");
    LLVMBuildStore(*builder, res, addr_rd);
}

void divu(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildUDiv(*builder, val_rs1, val_rs2, "udiv");
    LLVMBuildStore(*builder, res, addr_rd);
}

void rem(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildSRem(*builder, val_rs1, val_rs2, "srem");
    LLVMBuildStore(*builder, res, addr_rd);
}

void remu(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildURem(*builder, val_rs1, val_rs2, "urem");
    LLVMBuildStore(*builder, res, addr_rd);
}

void caddi4spn(LLVMBuilderRef *builder UNUSED,
               LLVMTypeRef *param_types UNUSED,
               LLVMValueRef start UNUSED,
               LLVMBasicBlockRef *entry UNUSED,
               LLVMBuilderRef *taken_builder UNUSED,
               LLVMBuilderRef *untaken_builder UNUSED,
               uint64_t mem_base UNUSED,
               rv_insn_t *ir UNUSED)
{
    LLVMValueRef sp_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + rv_reg_sp, true)};
    LLVMValueRef addr_sp =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              sp_offset, 1, "addr_sp");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_sp =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_sp, "val_sp");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt32Type(), (int16_t) ir->imm, true), "add");
    LLVMBuildStore(*builder, res, addr_rd);
}
void clw(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
    LLVMBuildStore(*builder, res, addr_rd);
}

void csw(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
}

void cnop(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
}

void caddi(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_rd,
        LLVMConstInt(LLVMInt32Type(), (int16_t) ir->imm, true), "add");
    LLVMBuildStore(*builder, res, addr_rd);
}

void cjal(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef ra_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + rv_reg_ra, true)};
    LLVMValueRef addr_ra =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              ra_offset, 1, "addr_ra");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true),
                   addr_ra);
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    if (ir->branch_taken) {
        *taken_builder = *builder;
    } else {
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(*builder);
    }
}

void cli(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   addr_rd);
}

void caddi16sp(LLVMBuilderRef *builder UNUSED,
               LLVMTypeRef *param_types UNUSED,
               LLVMValueRef start UNUSED,
               LLVMBasicBlockRef *entry UNUSED,
               LLVMBuilderRef *taken_builder UNUSED,
               LLVMBuilderRef *untaken_builder UNUSED,
               uint64_t mem_base UNUSED,
               rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_rd, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "add");
    LLVMBuildStore(*builder, res, addr_rd);
}

void clui(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   addr_rd);
}
void csrli(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res =
        LLVMBuildLShr(*builder, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), ir->shamt, true), "srl");
    LLVMBuildStore(*builder, res, addr_rs1);
}

void csrai(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res =
        LLVMBuildAShr(*builder, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), ir->shamt, true), "sra");
    LLVMBuildStore(*builder, res, addr_rs1);
}

void candi(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildAnd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "and");
    LLVMBuildStore(*builder, res, addr_rs1);
}

void csub(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildSub(*builder, val_rs1, val_rs2, "sub");
    LLVMBuildStore(*builder, res, addr_rd);
}
void cxor(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildXor(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, addr_rd);
}

void cor(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildOr(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, addr_rd);
}

void cand(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildAnd(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, addr_rd);
}

void cj(LLVMBuilderRef *builder UNUSED,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start UNUSED,
        LLVMBasicBlockRef *entry UNUSED,
        LLVMBuilderRef *taken_builder UNUSED,
        LLVMBuilderRef *untaken_builder UNUSED,
        uint64_t mem_base UNUSED,
        rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    if (ir->branch_taken) {
        *taken_builder = *builder;
    } else {
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(*builder);
    }
}

void cbeqz(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntEQ, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), 0, true), "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void cbnez(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntNE, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), 0, true), "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken) {
        *taken_builder = builder2;
    } else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken) {
        *untaken_builder = builder3;
    } else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void cslli(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildShl(
        *builder, val_rd,
        LLVMConstInt(LLVMInt32Type(), (uint8_t) ir->imm, true), "sll");
    LLVMBuildStore(*builder, res, addr_rd);
}

void clwsp(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef sp_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + rv_reg_sp, true)};
    LLVMValueRef addr_sp =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              sp_offset, 1, "addr_sp");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_sp = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_sp, "val_sp"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
    LLVMBuildStore(*builder, res, addr_rd);
}

void cjr(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, val_rs1, addr_PC);
    LLVMBuildRetVoid(*builder);
}

void cmv(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         LLVMBasicBlockRef *entry UNUSED,
         LLVMBuilderRef *taken_builder UNUSED,
         LLVMBuilderRef *untaken_builder UNUSED,
         uint64_t mem_base UNUSED,
         rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMBuildStore(*builder, val_rs2, addr_rd);
}

void cebreak(LLVMBuilderRef *builder UNUSED,
             LLVMTypeRef *param_types UNUSED,
             LLVMValueRef start UNUSED,
             LLVMBasicBlockRef *entry UNUSED,
             LLVMBuilderRef *taken_builder UNUSED,
             LLVMBuilderRef *untaken_builder UNUSED,
             uint64_t mem_base UNUSED,
             rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   addr_PC);
    LLVMValueRef ebreak_offset[1] = {LLVMConstInt(LLVMInt32Type(), 9, true)};
    LLVMValueRef addr_io = LLVMBuildInBoundsGEP2(
        *builder, LLVMPointerType(LLVMVoidType(), 0), LLVMGetParam(start, 0),
        ebreak_offset, 1, "addr_rv");
    LLVMValueRef func_ebreak = LLVMBuildLoad2(
        *builder,
        LLVMPointerType(LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), 0),
        addr_io, "func_ebreak");
    LLVMValueRef ebreak_param[1] = {LLVMGetParam(start, 0)};
    LLVMBuildCall2(*builder,
                   LLVMFunctionType(LLVMVoidType(), param_types, 1, 0),
                   func_ebreak, ebreak_param, 1, "");
    LLVMBuildRetVoid(*builder);
}

void cjalr(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef ra_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + rv_reg_ra, true)};
    LLVMValueRef addr_ra =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              ra_offset, 1, "addr_ra");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true),
                   addr_ra);
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, val_rs1, addr_PC);
    LLVMBuildRetVoid(*builder);
}

void cadd(LLVMBuilderRef *builder UNUSED,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start UNUSED,
          LLVMBasicBlockRef *entry UNUSED,
          LLVMBuilderRef *taken_builder UNUSED,
          LLVMBuilderRef *untaken_builder UNUSED,
          uint64_t mem_base UNUSED,
          rv_insn_t *ir UNUSED)
{
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, addr_rd);
}

void cswsp(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef sp_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + rv_reg_sp, true)};
    LLVMValueRef addr_sp =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              sp_offset, 1, "addr_sp");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_sp = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_sp, "val_sp"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
}

void fuse1(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        LLVMValueRef rd_offset[1] = {LLVMConstInt(
            LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + fuse[i].rd,
            true)};
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),
                                                     LLVMGetParam(start, 0),
                                                     rd_offset, 1, "addr_rd");
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), fuse[i].imm, true),
                       addr_rd);
    }
}

void fuse2(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   addr_rd);
    LLVMValueRef rs1_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs1, true)};
    LLVMValueRef addr_rs1 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs1_offset, 1, "addr_rs1");
    LLVMValueRef rs2_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rs2, true)};
    LLVMValueRef addr_rs2 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rs2_offset, 1, "addr_rs2");
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rd, "add");
    LLVMBuildStore(*builder, res, addr_rs2);
}

void fuse3(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        LLVMValueRef rs1_offset[1] = {LLVMConstInt(
            LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + fuse[i].rs1,
            true)};
        LLVMValueRef addr_rs1 = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32Type(), LLVMGetParam(start, 0), rs1_offset, 1,
            "addr_rs1");
        LLVMValueRef rs2_offset[1] = {LLVMConstInt(
            LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + fuse[i].rs2,
            true)};
        LLVMValueRef addr_rs2 = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32Type(), LLVMGetParam(start, 0), rs2_offset, 1,
            "addr_rs2");
        LLVMValueRef val_rs1 = LLVMBuildZExt(
            *builder,
            LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
            LLVMInt64Type(), "zext32to64");
        LLVMValueRef val_rs2 =
            LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
        LLVMValueRef addr = LLVMBuildAdd(
            *builder, val_rs1,
            LLVMConstInt(LLVMInt64Type(), fuse[i].imm + mem_base, true),
            "addr");
        LLVMValueRef cast_addr = LLVMBuildIntToPtr(
            *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
        LLVMBuildStore(*builder, val_rs2, cast_addr);
    }
}

void fuse4(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        LLVMValueRef rs1_offset[1] = {LLVMConstInt(
            LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + fuse[i].rs1,
            true)};
        LLVMValueRef addr_rs1 = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32Type(), LLVMGetParam(start, 0), rs1_offset, 1,
            "addr_rs1");
        LLVMValueRef rd_offset[1] = {LLVMConstInt(
            LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + fuse[i].rd,
            true)};
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),
                                                     LLVMGetParam(start, 0),
                                                     rd_offset, 1, "addr_rd");
        LLVMValueRef val_rs1 = LLVMBuildZExt(
            *builder,
            LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1"),
            LLVMInt64Type(), "zext32to64");
        LLVMValueRef addr = LLVMBuildAdd(
            *builder, val_rs1,
            LLVMConstInt(LLVMInt64Type(), fuse[i].imm + mem_base, true),
            "addr");
        LLVMValueRef cast_addr = LLVMBuildIntToPtr(
            *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
        LLVMValueRef res =
            LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
        LLVMBuildStore(*builder, res, addr_rd);
    }
}

void fuse5(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   addr_PC);
    LLVMValueRef ecall_offset[1] = {LLVMConstInt(LLVMInt32Type(), 10, true)};
    LLVMValueRef addr_io = LLVMBuildInBoundsGEP2(
        *builder, LLVMPointerType(LLVMVoidType(), 0), LLVMGetParam(start, 0),
        ecall_offset, 1, "addr_rv");
    LLVMValueRef func_ecall = LLVMBuildLoad2(
        *builder,
        LLVMPointerType(LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), 0),
        addr_io, "func_ecall");
    LLVMValueRef ecall_param[1] = {LLVMGetParam(start, 0)};
    LLVMBuildCall2(*builder,
                   LLVMFunctionType(LLVMVoidType(), param_types, 1, 0),
                   func_ecall, ecall_param, 1, "");
    LLVMBuildRetVoid(*builder);
}

void fuse6(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    LLVMValueRef PC_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_PC =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              PC_offset, 1, "addr_PC");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   addr_PC);
    LLVMValueRef ecall_offset[1] = {LLVMConstInt(LLVMInt32Type(), 11, true)};
    LLVMValueRef addr_io = LLVMBuildInBoundsGEP2(
        *builder, LLVMPointerType(LLVMVoidType(), 0), LLVMGetParam(start, 0),
        ecall_offset, 1, "addr_rv");
    LLVMValueRef func_ecall = LLVMBuildLoad2(
        *builder,
        LLVMPointerType(LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), 0),
        addr_io, "func_ecall");
    LLVMValueRef ecall_param[1] = {LLVMGetParam(start, 0)};
    LLVMBuildCall2(*builder,
                   LLVMFunctionType(LLVMVoidType(), param_types, 1, 0),
                   func_ecall, ecall_param, 1, "");
    LLVMBuildRetVoid(*builder);
}

void fuse7(LLVMBuilderRef *builder UNUSED,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start UNUSED,
           LLVMBasicBlockRef *entry UNUSED,
           LLVMBuilderRef *taken_builder UNUSED,
           LLVMBuilderRef *untaken_builder UNUSED,
           uint64_t mem_base UNUSED,
           rv_insn_t *ir UNUSED)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            slli(builder, param_types, start, entry, taken_builder,
                 untaken_builder, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        case rv_insn_srli:
            srli(builder, param_types, start, entry, taken_builder,
                 untaken_builder, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        case rv_insn_srai:
            srai(builder, param_types, start, entry, taken_builder,
                 untaken_builder, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        default:
            __UNREACHABLE;
            break;
        }
    }
}

FORCE_INLINE bool insn_is_unconditional_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_ecall:
    case rv_insn_ebreak:
    case rv_insn_jalr:
    case rv_insn_mret:
    case rv_insn_fuse5:
    case rv_insn_fuse6:
#if RV32_HAS(EXT_C)
    case rv_insn_cjalr:
    case rv_insn_cjr:
    case rv_insn_cebreak:
#endif
        return true;
    }
    return false;
}

static void trace_ebb(LLVMBuilderRef *builder,
                      LLVMTypeRef *param_types UNUSED,
                      LLVMValueRef start,
                      LLVMBasicBlockRef *entry,
                      uint64_t mem_base,
                      rv_insn_t *ir,
                      set_t *set,
                      struct LLVM_block_map *map)
{
    if (set_has(set, ir->pc))
        return;
    set_add(set, ir->pc);
    struct LLVM_block_map_entry map_entry;
    map_entry.block = *entry;
    map_entry.pc = ir->pc;
    map->map[map->count++] = map_entry;
    LLVMBuilderRef tk, utk;
    while (1) {
        switch (ir->opcode) {
        case rv_insn_nop:
            nop(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_lui:
            lui(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_auipc:
            auipc(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_jal:
            jal(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_jalr:
            jalr(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_beq:
            beq(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_bne:
            bne(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_blt:
            blt(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_bge:
            bge(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_bltu:
            bltu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_bgeu:
            bgeu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_lb:
            lb(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_lh:
            lh(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_lw:
            lw(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_lbu:
            lbu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_lhu:
            lhu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sb:
            sb(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sh:
            sh(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sw:
            sw(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_addi:
            addi(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_slti:
            slti(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sltiu:
            sltiu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_xori:
            xori(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_ori:
            ori(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_andi:
            andi(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_slli:
            slli(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_srli:
            srli(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_srai:
            srai(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_add:
            add(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sub:
            sub(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sll:
            sll(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_slt:
            slt(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sltu:
            sltu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_xor:
            xor(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_srl:
            srl(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_sra:
            sra(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_or:
            or (builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_and:
            and(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_ecall:
            ecall(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_ebreak:
            ebreak(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_mul:
            mul(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_mulh:
            mulh(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_mulhsu:
            mulhsu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_mulhu:
            mulhu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_div:
            divaaa(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_divu:
            divu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_rem:
            rem(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_remu:
            remu(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_caddi4spn:
            caddi4spn(builder, param_types, start, entry, &tk, &utk, mem_base,
                      ir);
            break;
        case rv_insn_clw:
            clw(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_csw:
            csw(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cnop:
            cnop(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_caddi:
            caddi(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cjal:
            cjal(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cli:
            cli(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_caddi16sp:
            caddi16sp(builder, param_types, start, entry, &tk, &utk, mem_base,
                      ir);
            break;
        case rv_insn_clui:
            clui(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_csrli:
            csrli(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_csrai:
            csrai(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_candi:
            candi(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_csub:
            csub(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cxor:
            cxor(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cor:
            cor(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cand:
            cand(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cj:
            cj(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cbeqz:
            cbeqz(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cbnez:
            cbnez(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cslli:
            cslli(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_clwsp:
            clwsp(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cjr:
            cjr(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cmv:
            cmv(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cebreak:
            cebreak(builder, param_types, start, entry, &tk, &utk, mem_base,
                    ir);
            break;
        case rv_insn_cjalr:
            cjalr(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cadd:
            cadd(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_cswsp:
            cswsp(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_fuse1:
            fuse1(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_fuse2:
            fuse2(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_fuse3:
            fuse3(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_fuse4:
            fuse4(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_fuse5:
            fuse5(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_fuse6:
            fuse6(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        case rv_insn_fuse7:
            fuse7(builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            break;
        default:
            assert(NULL);
        }
        if (!ir->next)
            break;
        ir = ir->next;
    }
    if (!insn_is_unconditional_branch(ir->opcode)) {
        if (ir->branch_untaken) {
            if (set_has(set, ir->branch_untaken->pc)) {
                for (uint32_t i = 0; i < map->count; i++) {
                    if (map->map[i].pc == ir->branch_untaken->pc) {
                        LLVMBuildBr(utk, map->map[i].block);
                        break;
                    }
                }
            } else {
                LLVMBasicBlockRef untaken_entry =
                    LLVMAppendBasicBlock(start, "untaken_entry");
                LLVMBuilderRef untaken_builder = LLVMCreateBuilder();
                LLVMPositionBuilderAtEnd(untaken_builder, untaken_entry);
                LLVMBuildBr(utk, untaken_entry);
                trace_ebb(&untaken_builder, param_types, start, &untaken_entry,
                          mem_base, ir->branch_untaken, set, map);
            }
        }
        if (ir->branch_taken) {
            if (set_has(set, ir->branch_taken->pc)) {
                for (uint32_t i = 0; i < map->count; i++) {
                    if (map->map[i].pc == ir->branch_taken->pc) {
                        LLVMBuildBr(tk, map->map[i].block);
                        break;
                    }
                }
            } else {
                LLVMBasicBlockRef taken_entry =
                    LLVMAppendBasicBlock(start, "taken_entry");
                LLVMBuilderRef taken_builder = LLVMCreateBuilder();
                LLVMPositionBuilderAtEnd(taken_builder, taken_entry);
                LLVMBuildBr(tk, taken_entry);
                trace_ebb(&taken_builder, param_types, start, &taken_entry,
                          mem_base, ir->branch_taken, set, map);
            }
        }
    }
}

funcPtr_t t2(rv_insn_t *ir, uint64_t mem_base)
{
    LLVMModuleRef module = LLVMModuleCreateWithName("my_module");
    LLVMTypeRef io_members[] = {
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMInt8Type()};
    LLVMTypeRef struct_io = LLVMStructType(io_members, 12, false);
    LLVMTypeRef arr_X = LLVMArrayType(LLVMInt32Type(), 32);
    LLVMTypeRef rv_members[] = {LLVMInt8Type(), struct_io, arr_X,
                                LLVMInt32Type()};
    LLVMTypeRef struct_rv = LLVMStructType(rv_members, 4, false);
    LLVMTypeRef param_types[] = {LLVMPointerType(struct_rv, 0)};
    LLVMValueRef start = LLVMAddFunction(
        module, "start", LLVMFunctionType(LLVMVoidType(), param_types, 1, 0));
    LLVMBasicBlockRef first_block = LLVMAppendBasicBlock(start, "first_block");
    LLVMBuilderRef first_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(first_builder, first_block);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(start, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMBuildBr(first_builder, entry);
    set_t set;
    set_reset(&set);
    struct LLVM_block_map map;
    map.count = 0;
    trace_ebb(&builder, param_types, start, &entry, mem_base, ir, &set, &map);

    char *error = NULL, *triple = LLVMGetDefaultTargetTriple();
    LLVMExecutionEngineRef engine;
    LLVMTargetRef target;
    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
        fprintf(stderr, "failed to create Target\n");
    }
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, LLVMGetHostCPUName(), LLVMGetHostCPUFeatures(),
        LLVMCodeGenLevelNone, LLVMRelocDefault, LLVMCodeModelJITDefault);
    LLVMPassBuilderOptionsRef pb_option = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(module,
                  "default<O3>,dce,early-cse<memssa>,instcombine,memcpyopt", tm,
                  pb_option);
    // if (LLVMPrintModuleToFile(module, "sum.ll", NULL)) {
    //     fprintf(stderr, "error writing bitcode to file, skipping\n");
    // }
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr, "failed to create execution engine\n");
        abort();
    }

    // if (error) {
    //     fprintf(stderr, "error: %s\n", error);
    //     LLVMDisposeMessage(error);
    //     exit(EXIT_FAILURE);
    // }
    funcPtr_t funcPtr = (funcPtr_t) LLVMGetPointerToGlobal(engine, start);
    return funcPtr;
}