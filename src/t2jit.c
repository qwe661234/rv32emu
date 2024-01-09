#include <assert.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "riscv.h"
#include "riscv_private.h"


typedef intptr_t (*funcPtr_t)(riscv_t *);

void nop(LLVMBuilderRef *builder UNUSED,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start UNUSED,
         rv_insn_t *ir UNUSED)
{
    return;
}

void lui(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
{
    LLVMValueRef rd_offset[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true)};
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              rd_offset, 1, "addr_rd");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   addr_rd);
}

void auipc(LLVMBuilderRef *builder,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start,
           rv_insn_t *ir)
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

void jal(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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
    LLVMBuildStore(*builder,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_PC);
    LLVMBuildRetVoid(*builder);
}

void jalr(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void beq(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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
    LLVMBuildStore(builder2,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_PC);
    LLVMBuildRetVoid(builder2);
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                   addr_PC);
    LLVMBuildRetVoid(builder3);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bne(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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
    LLVMBuildStore(builder2,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_PC);
    LLVMBuildRetVoid(builder2);
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                   addr_PC);
    LLVMBuildRetVoid(builder3);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void blt(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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
    LLVMBuildStore(builder2,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_PC);
    LLVMBuildRetVoid(builder2);
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                   addr_PC);
    LLVMBuildRetVoid(builder3);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bge(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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
    LLVMBuildStore(builder2,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_PC);
    LLVMBuildRetVoid(builder2);
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                   addr_PC);
    LLVMBuildRetVoid(builder3);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bltu(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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
    LLVMBuildStore(builder2,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_PC);
    LLVMBuildRetVoid(builder2);
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                   addr_PC);
    LLVMBuildRetVoid(builder3);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void bgeu(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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
    LLVMBuildStore(builder2,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   addr_PC);
    LLVMBuildRetVoid(builder2);
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                   addr_PC);
    LLVMBuildRetVoid(builder3);
    LLVMBuildCondBr(*builder, cond, taken, untaken);
}

void lb(LLVMBuilderRef *builder,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start,
        LLVMBasicBlockRef *entry,
        uint64_t mem_base,
        rv_insn_t *ir)
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

void lh(LLVMBuilderRef *builder,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start,
        LLVMBasicBlockRef *entry,
        uint64_t mem_base,
        rv_insn_t *ir)
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

void lw(LLVMBuilderRef *builder,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start,
        LLVMBasicBlockRef *entry,
        uint64_t mem_base,
        rv_insn_t *ir)
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

void lbu(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         LLVMBasicBlockRef *entry,
         uint64_t mem_base,
         rv_insn_t *ir)
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

void lhu(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         LLVMBasicBlockRef *entry,
         uint64_t mem_base,
         rv_insn_t *ir)
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

void sb(LLVMBuilderRef *builder,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start,
        LLVMBasicBlockRef *entry,
        uint64_t mem_base,
        rv_insn_t *ir)
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

void sh(LLVMBuilderRef *builder,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start,
        LLVMBasicBlockRef *entry,
        uint64_t mem_base,
        rv_insn_t *ir)
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

void sw(LLVMBuilderRef *builder,
        LLVMTypeRef *param_types UNUSED,
        LLVMValueRef start,
        LLVMBasicBlockRef *entry,
        uint64_t mem_base,
        rv_insn_t *ir)
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

void addi(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void slti(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          LLVMBasicBlockRef *entry,
          rv_insn_t *ir)
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

void sltiu(LLVMBuilderRef *builder,
           LLVMTypeRef *param_types UNUSED,
           LLVMValueRef start,
           LLVMBasicBlockRef *entry,
           rv_insn_t *ir)
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

void xori(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void ori(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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

void andi(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void slli(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void srli(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void srai(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void add(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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

void sub(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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

void sll(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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

void slt(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         LLVMBasicBlockRef *entry,
         rv_insn_t *ir)
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

void sltu(LLVMBuilderRef *builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          LLVMBasicBlockRef *entry,
          rv_insn_t *ir)
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
    (LLVMBuilderRef * builder,
     LLVMTypeRef *param_types UNUSED,
     LLVMValueRef start,
     rv_insn_t *ir) {
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

    void srl(LLVMBuilderRef *builder,
             LLVMTypeRef *param_types UNUSED,
             LLVMValueRef start,
             rv_insn_t *ir)
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

void sra(LLVMBuilderRef *builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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

void or (LLVMBuilderRef * builder,
         LLVMTypeRef *param_types UNUSED,
         LLVMValueRef start,
         rv_insn_t *ir)
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

void and (LLVMBuilderRef * builder,
          LLVMTypeRef *param_types UNUSED,
          LLVMValueRef start,
          rv_insn_t *ir)
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

void ecall(LLVMBuilderRef *builder,
           LLVMTypeRef *param_types,
           LLVMValueRef start,
           rv_insn_t *ir)
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

void ebreak(LLVMBuilderRef *builder,
            LLVMTypeRef *param_types,
            LLVMValueRef start,
            rv_insn_t *ir)
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

static const char *name_table[] = {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = #inst,
    RV_INSN_LIST
#undef _
};

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
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(start, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);
    while (1) {
        // printf("%s\n", name_table[ir->opcode]);
        switch (ir->opcode) {
        case rv_insn_nop:
            nop(&builder, param_types, start, ir);
            break;
        case rv_insn_lui:
            lui(&builder, param_types, start, ir);
            break;
        case rv_insn_auipc:
            auipc(&builder, param_types, start, ir);
            break;
        case rv_insn_jal:
            jal(&builder, param_types, start, ir);
            break;
        case rv_insn_jalr:
            jalr(&builder, param_types, start, ir);
            break;
        case rv_insn_beq:
            beq(&builder, param_types, start, ir);
            break;
        case rv_insn_bne:
            bne(&builder, param_types, start, ir);
            break;
        case rv_insn_blt:
            blt(&builder, param_types, start, ir);
            break;
        case rv_insn_bge:
            bge(&builder, param_types, start, ir);
            break;
        case rv_insn_bltu:
            bltu(&builder, param_types, start, ir);
            break;
        case rv_insn_bgeu:
            bgeu(&builder, param_types, start, ir);
            break;
        case rv_insn_lb:
            lb(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_lh:
            lh(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_lw:
            lw(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_lbu:
            lbu(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_lhu:
            lhu(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_sb:
            sb(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_sh:
            sh(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_sw:
            sw(&builder, param_types, start, &entry, mem_base, ir);
            break;
        case rv_insn_addi:
            addi(&builder, param_types, start, ir);
            break;
        case rv_insn_slti:
            slti(&builder, param_types, start, &entry, ir);
            break;
        case rv_insn_sltiu:
            sltiu(&builder, param_types, start, &entry, ir);
            break;
        case rv_insn_xori:
            xori(&builder, param_types, start, ir);
            break;
        case rv_insn_ori:
            ori(&builder, param_types, start, ir);
            break;
        case rv_insn_andi:
            andi(&builder, param_types, start, ir);
            break;
        case rv_insn_slli:
            slli(&builder, param_types, start, ir);
            break;
        case rv_insn_srli:
            srli(&builder, param_types, start, ir);
            break;
        case rv_insn_srai:
            srai(&builder, param_types, start, ir);
            break;
        case rv_insn_add:
            add(&builder, param_types, start, ir);
            break;
        case rv_insn_sub:
            sub(&builder, param_types, start, ir);
            break;
        case rv_insn_sll:
            sll(&builder, param_types, start, ir);
            break;
        case rv_insn_slt:
            slt(&builder, param_types, start, &entry, ir);
            break;
        case rv_insn_sltu:
            sltu(&builder, param_types, start, &entry, ir);
            break;
        case rv_insn_xor:
            xor(&builder, param_types, start, ir);
            break;
        case rv_insn_srl:
            srl(&builder, param_types, start, ir);
            break;
        case rv_insn_sra:
            sra(&builder, param_types, start, ir);
            break;
        case rv_insn_or:
            or (&builder, param_types, start, ir);
            break;
        case rv_insn_and:
            and(&builder, param_types, start, ir);
            break;
        case rv_insn_ecall:
            ecall(&builder, param_types, start, ir);
            break;
        case rv_insn_ebreak:
            ebreak(&builder, param_types, start, ir);
            break;
        default:
            assert(NULL);
        }
        if (!ir->next)
            break;
        ir = ir->next;
    }

    if (LLVMPrintModuleToFile(module, "start.ll", NULL)) {
        fprintf(stderr, "error writing bitcode to file, skipping\n");
    }

    /* DEBUG */
    char *error = NULL;
    LLVMVerifyModule(module, LLVMAbortProcessAction, &error);
    LLVMDisposeMessage(error);
    /* DEBUG */

    LLVMExecutionEngineRef engine;
    error = NULL;
    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr, "failed to create execution engine\n");
        abort();
    }

    if (error) {
        fprintf(stderr, "error: %s\n", error);
        LLVMDisposeMessage(error);
        exit(EXIT_FAILURE);
    }

    funcPtr_t funcPtr = (funcPtr_t) LLVMGetPointerToGlobal(engine, start);

    if (LLVMPrintModuleToFile(module, "start.ll", NULL)) {
        fprintf(stderr, "error writing bitcode to file, skipping\n");
    }
    // LLVMDisposeBuilder(builder);
    // LLVMDisposeExecutionEngine(engine);
    return funcPtr;
}