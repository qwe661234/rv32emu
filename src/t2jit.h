#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "riscv.h"
#include "riscv_private.h"

typedef int32_t (*funcPtr_t)(riscv_t *);

funcPtr_t t2()
{
    LLVMModuleRef module = LLVMModuleCreateWithName("my_module");
    LLVMTypeRef io = LLVMArrayType(LLVMInt64Type(), 12);
    LLVMTypeRef arrX = LLVMArrayType(LLVMInt32Type(), 32);
    LLVMTypeRef members[] = {LLVMInt8Type(), io, arrX, LLVMInt32Type()};
    LLVMTypeRef struct_rv = LLVMStructType(members, 4, true);
    LLVMTypeRef param_types[] = {LLVMPointerType(struct_rv, 0)};
    LLVMTypeRef ret_type = LLVMFunctionType(LLVMVoidType(), param_types, 1, 0);
    LLVMValueRef start = LLVMAddFunction(module, "start", ret_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(start, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMValueRef indexes[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, PC) / sizeof(int), true)};
    LLVMValueRef addr_rvPC =
        LLVMBuildInBoundsGEP2(builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              indexes, 1, "addr_rvPC");
    LLVMValueRef val_PC =
        LLVMBuildLoad2(builder, LLVMInt32Type(), addr_rvPC, "val_PC");
    LLVMValueRef indexes2[1] = {LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + 30, true)};
    LLVMValueRef addr_rvX =
        LLVMBuildInBoundsGEP2(builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              indexes2, 1, "addr_rvX");
    LLVMBuildStore(builder, val_PC, addr_rvX);
    // // if
    LLVMValueRef cond = LLVMBuildICmp(
        builder, LLVMIntEQ, LLVMConstInt(LLVMInt32Type(), 0, true),
        LLVMConstInt(LLVMInt32Type(), 0, true), "cond");
    // // then
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    LLVMBuildStore(builder2, LLVMConstInt(LLVMInt32Type(), 1, true), addr_rvX);
    LLVMBuildRetVoid(builder2);
    // // else
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    LLVMBuildStore(builder3, LLVMConstInt(LLVMInt32Type(), 0, true), addr_rvX);
    LLVMBuildRetVoid(builder3);
    LLVMBuildCondBr(builder, cond, taken, untaken);

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