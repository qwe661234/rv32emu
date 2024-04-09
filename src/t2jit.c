#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <assert.h>
#include <stdlib.h>

#include "riscv_private.h"
#include "t2jit.h"

#define MAX_BLOCKS 8152

struct LLVM_block_map_entry {
    uint32_t pc;
    LLVMBasicBlockRef block;
};

struct LLVM_block_map {
    uint32_t count;
    struct LLVM_block_map_entry map[MAX_BLOCKS];
};

#define RVT2OP(inst, code)                                                \
    static void t2_##inst(                                                \
        LLVMBuilderRef *builder UNUSED, LLVMTypeRef *param_types UNUSED,  \
        LLVMValueRef start UNUSED, LLVMBasicBlockRef *entry UNUSED,       \
        LLVMBuilderRef *taken_builder UNUSED,                             \
        LLVMBuilderRef *untaken_builder UNUSED, uint64_t mem_base UNUSED, \
        rv_insn_t *ir UNUSED)                                             \
    {                                                                     \
        code;                                                             \
    }

#include "t2_rv32_template.c"
#undef RVT2OP

static const void *dispatch_table[] = {
/* RV32 instructions */
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = t2_##inst,
    RV_INSN_LIST
#undef _
/* Macro operation fusion instructions */
#define _(inst) [rv_insn_##inst] = t2_##inst,
        FUSE_INSN_LIST
#undef _
};

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

FORCE_INLINE bool insn_is_branch(uint8_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    IIF(can_branch)                                           \
    (case rv_insn_##inst:, )
        RV_INSN_LIST
#undef _
        return true;
    }
    return false;
}

typedef void (*t2_codegen_block_func_t)(LLVMBuilderRef *builder UNUSED,
                                        LLVMTypeRef *param_types UNUSED,
                                        LLVMValueRef start UNUSED,
                                        LLVMBasicBlockRef *entry UNUSED,
                                        LLVMBuilderRef *taken_builder UNUSED,
                                        LLVMBuilderRef *untaken_builder UNUSED,
                                        uint64_t mem_base UNUSED,
                                        rv_insn_t *ir UNUSED);

static const uint8_t insn_len_table[] = {
/* RV32 instructions */
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = insn_len,
    RV_INSN_LIST
#undef _
};

static void trace_range(riscv_t *rv,
                        LLVMBuilderRef *builder,
                        LLVMTypeRef *param_types,
                        LLVMValueRef start,
                        LLVMBasicBlockRef *entry,
                        uint64_t mem_base,
                        rv_insn_t *ir,
                        uint32_t pc,
                        set_t *set,
                        struct LLVM_block_map *map)
{
    if (set_has(set, pc))
        return;
    set_add(set, pc);
    struct LLVM_block_map_entry map_entry;
    map_entry.block = *entry;
    map_entry.pc = pc;
    map->map[map->count++] = map_entry;
    LLVMBuilderRef tk, utk;

    if (!ir) {
        ir = malloc(sizeof(rv_insn_t));
        while (1) {
            memset(ir, 0, sizeof(rv_insn_t));

            const uint32_t insn = rv->io.mem_ifetch(pc);

            if (!rv_decode(ir, insn))
                assert(NULL);
            ir->pc = pc;
            ((t2_codegen_block_func_t) dispatch_table[ir->opcode])(
                builder, param_types, start, entry, &tk, &utk, mem_base, ir);

            if (insn_is_branch(ir->opcode))
                break;
            pc += insn_len_table[ir->opcode];
        }
    } else {
        while (1) {
            ((t2_codegen_block_func_t) dispatch_table[ir->opcode])(
                builder, param_types, start, entry, &tk, &utk, mem_base, ir);
            if (!ir->next)
                break;
            ir = ir->next;
        }
        pc = ir->pc;
    }
    if (!insn_is_unconditional_branch(ir->opcode)) {
        if (ir->opcode != rv_insn_jal && ir->opcode != rv_insn_cjal &&
            ir->opcode != rv_insn_cj) {
            uint32_t utk_pc = pc + insn_len_table[ir->opcode];
            if (set_has(set, utk_pc)) {
                for (uint32_t i = 0; i < map->count; i++) {
                    if (map->map[i].pc == utk_pc) {
                        LLVMBuildBr(utk, map->map[i].block);
                        break;
                    }
                }
            } else {
                LLVMBasicBlockRef untaken_entry =
                    LLVMAppendBasicBlock(start,
                                         "untaken_"
                                         "entry");
                LLVMBuilderRef untaken_builder = LLVMCreateBuilder();
                LLVMPositionBuilderAtEnd(untaken_builder, untaken_entry);
                LLVMBuildBr(utk, untaken_entry);
                trace_range(rv, &untaken_builder, param_types, start,
                            &untaken_entry, mem_base, ir->branch_untaken,
                            utk_pc, set, map);
            }
        }
        uint32_t tk_pc = pc + ir->imm;
        if (set_has(set, tk_pc)) {
            for (uint32_t i = 0; i < map->count; i++) {
                if (map->map[i].pc == tk_pc) {
                    LLVMBuildBr(tk, map->map[i].block);
                    break;
                }
            }
        } else {
            LLVMBasicBlockRef taken_entry = LLVMAppendBasicBlock(start,
                                                                 "taken_"
                                                                 "entry");
            LLVMBuilderRef taken_builder = LLVMCreateBuilder();
            LLVMPositionBuilderAtEnd(taken_builder, taken_entry);
            LLVMBuildBr(tk, taken_entry);
            trace_range(rv, &taken_builder, param_types, start, &taken_entry,
                        mem_base, ir->branch_taken, tk_pc, set, map);
        }
    }
}

void t2(riscv_t *rv, block_t *block, uint64_t mem_base)
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
    trace_range(rv, &builder, param_types, start, &entry, mem_base, block->ir_head,
                block->pc_start, &set, &map);
    char *error = NULL, *triple = LLVMGetDefaultTargetTriple();
    LLVMExecutionEngineRef engine;
    LLVMTargetRef target;
    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
        fprintf(stderr,
                "failed to create "
                "Target\n");
    }
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, LLVMGetHostCPUName(), LLVMGetHostCPUFeatures(),
        LLVMCodeGenLevelNone, LLVMRelocDefault, LLVMCodeModelJITDefault);
    LLVMPassBuilderOptionsRef pb_option = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(module,
                  "default<O3>,dce,early-cse<"
                  "memssa>,instcombine,memcpyopt",
                  tm, pb_option);

    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr,
                "failed to create "
                "execution engine\n");
        abort();
    }

    block->func = (funcPtr_t) LLVMGetPointerToGlobal(engine, start);
    block->hot2 = true;
}