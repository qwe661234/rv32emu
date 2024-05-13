/* This file maps each custom IR to the corresponding LLVM IRs and builds LLVM
 * IR through LLVM-C API. The built LLVM IR is offloaded to the LLVM backend,
 * where it undergoes optimization through several selected LLVM passes.
 * Subsequently, the optimized LLVM IR is passed to the LLVM execution engine,
 * which compiles the optimized LLVM IR and returns a function pointer to the
 * generated machine code.
 */

RVT2OP(nop, { return; })

RVT2OP(lui, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   build_rd_addr(start, builder, ir));
})

RVT2OP(auipc, {
    LLVMBuildStore(*builder,
                   LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                   build_rd_addr(start, builder, ir));
})

RVT2OP(jal, {
    if (ir->rd)
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                       build_rd_addr(start, builder, ir));

    if (ir->branch_taken)
        *taken_builder = *builder;
    else {
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       build_PC_addr(start, builder, ir));
        LLVMBuildRetVoid(*builder);
    }
})

RVT2OP(jalr, {
    if (ir->rd)
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true),
                       build_rd_addr(start, builder, ir));
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res1 = LLVMBuildAdd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "add");
    LLVMValueRef res2 = LLVMBuildAnd(
        *builder, res1, LLVMConstInt(LLVMInt32Type(), ~1U, true), "and");
    LLVMBuildStore(*builder, res2, build_PC_addr(start, builder, ir));
    LLVMBuildRetVoid(*builder);
})

RVT2OP(beq, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntEQ, val_rs1, val_rs2, "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken)
        *taken_builder = builder2;
    else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken)
        *untaken_builder = builder3;
    else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 4, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
})

RVT2OP(bne, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
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
})

RVT2OP(blt, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
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
})

RVT2OP(bge, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
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
})

RVT2OP(bltu, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
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
})

RVT2OP(bgeu, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
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
})

RVT2OP(lb, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt8Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildSExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt8Type(), cast_addr, "res"),
        LLVMInt32Type(), "sext8to32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(lh, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt16Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildSExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt16Type(), cast_addr, "res"),
        LLVMInt32Type(), "sext16to32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(lw, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(lbu, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt8Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt8Type(), cast_addr, "res"),
        LLVMInt32Type(), "zext8to32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(lhu, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt16Type(), 0), "cast");
    LLVMValueRef res = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt16Type(), cast_addr, "res"),
        LLVMInt32Type(), "zext16to32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(sb, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt8Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt8Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
})

RVT2OP(sh, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt16Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt16Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
})

RVT2OP(sw, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
})

RVT2OP(addi, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "add");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(slti, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
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
})

RVT2OP(sltiu, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
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
})

RVT2OP(xori, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res = LLVMBuildXor(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "xor");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(ori, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res = LLVMBuildOr(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "or");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(andi, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res = LLVMBuildAnd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "and");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(slli, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res = LLVMBuildShl(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm & 0x1f, true),
        "sll");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(srli, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res = LLVMBuildLShr(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm & 0x1f, true),
        "srl");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(srai, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef res = LLVMBuildAShr(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm & 0x1f, true),
        "sra");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(add, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(sub, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildSub(*builder, val_rs1, val_rs2, "sub");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(sll, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef tmp =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef val_rs2 = LLVMBuildAnd(
        *builder, tmp, LLVMConstInt(LLVMInt32Type(), 0x1f, true), "and");
    LLVMValueRef res = LLVMBuildShl(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(slt, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
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
})

RVT2OP(sltu, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
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
})

RVT2OP(xor, {
  LLVMValueRef val_rs1 = LLVMBuildLoad2(
      *builder, LLVMInt32Type(), build_rs1_addr(start, builder, ir), "val_rs1");
  LLVMValueRef val_rs2 = LLVMBuildLoad2(
      *builder, LLVMInt32Type(), build_rs2_addr(start, builder, ir), "val_rs2");
  LLVMValueRef res = LLVMBuildXor(*builder, val_rs1, val_rs2, "xor");
  LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(srl, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef tmp =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef val_rs2 = LLVMBuildAnd(
        *builder, tmp, LLVMConstInt(LLVMInt32Type(), 0x1f, true), "and");
    LLVMValueRef res = LLVMBuildLShr(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(sra, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef tmp =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef val_rs2 = LLVMBuildAnd(
        *builder, tmp, LLVMConstInt(LLVMInt32Type(), 0x1f, true), "and");
    LLVMValueRef res = LLVMBuildAShr(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(or, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildOr(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(and, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildAnd(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(ecall, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   build_PC_addr(start, builder, ir));
    build_call_io_func(start, builder, param_types, 8);
    LLVMBuildRetVoid(*builder);
})

RVT2OP(ebreak, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   build_PC_addr(start, builder, ir));
    build_call_io_func(start, builder, param_types, 9);
    LLVMBuildRetVoid(*builder);
})

RVT2OP(wfi, { __UNREACHABLE; })

RVT2OP(uret, { __UNREACHABLE; })

RVT2OP(sret, { __UNREACHABLE; })

RVT2OP(hret, { __UNREACHABLE; })

RVT2OP(mret, { __UNREACHABLE; })

#if RV32_HAS(Zifencei)
RVT2OP(fencei, { __UNREACHABLE; })
#endif

#if RV32_HAS(Zicsr)
RVT2OP(csrrw, { __UNREACHABLE; })

RVT2OP(csrrs, { __UNREACHABLE; })

RVT2OP(csrrc, { __UNREACHABLE; })

RVT2OP(csrrwi, { __UNREACHABLE; })

RVT2OP(csrrsi, { __UNREACHABLE; })

RVT2OP(csrrci, { __UNREACHABLE; })
#endif

#if RV32_HAS(EXT_M)
RVT2OP(mul, {
    LLVMValueRef val_rs1 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "sextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2"),
        LLVMInt64Type(), "sextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildAnd(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                     LLVMConstInt(LLVMInt64Type(), 0xFFFFFFFF, false), "and");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(mulh, {
    LLVMValueRef val_rs1 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "sextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2"),
        LLVMInt64Type(), "sextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildLShr(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                      LLVMConstInt(LLVMInt64Type(), 32, false), "sll");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(mulhsu, {
    LLVMValueRef val_rs1 = LLVMBuildSExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "sextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2"),
        LLVMInt64Type(), "zextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildLShr(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                      LLVMConstInt(LLVMInt64Type(), 32, false), "sll");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(mulhu, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zextrs1to64");
    LLVMValueRef val_rs2 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2"),
        LLVMInt64Type(), "zextrs2to64");
    LLVMValueRef tmp =
        LLVMBuildLShr(*builder, LLVMBuildMul(*builder, val_rs1, val_rs2, "mul"),
                      LLVMConstInt(LLVMInt64Type(), 32, false), "sll");
    LLVMValueRef res =
        LLVMBuildTrunc(*builder, tmp, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(div, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildSDiv(*builder, val_rs1, val_rs2, "sdiv");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(divu, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildUDiv(*builder, val_rs1, val_rs2, "udiv");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(rem, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildSRem(*builder, val_rs1, val_rs2, "srem");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(remu, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildURem(*builder, val_rs1, val_rs2, "urem");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})
#endif

#if RV32_HAS(EXT_A)
RVT2OP(lrw, { __UNREACHABLE; })

RVT2OP(scw, { __UNREACHABLE; })

RVT2OP(amoswapw, { __UNREACHABLE; })

RVT2OP(amoaddw, { __UNREACHABLE; })

RVT2OP(amoxorw, { __UNREACHABLE; })

RVT2OP(amoandw, { __UNREACHABLE; })

RVT2OP(amoorw, { __UNREACHABLE; })

RVT2OP(amominw, { __UNREACHABLE; })

RVT2OP(amomaxw, { __UNREACHABLE; })

RVT2OP(amominuw, { __UNREACHABLE; })

RVT2OP(amomaxuw, { __UNREACHABLE; })
#endif

#if RV32_HAS(EXT_F)
RVT2OP(flw, { __UNREACHABLE; })

RVT2OP(fsw, { __UNREACHABLE; })

RVT2OP(fmadds, { __UNREACHABLE; })

RVT2OP(fmsubs, { __UNREACHABLE; })

RVT2OP(fnmsubs, { __UNREACHABLE; })

RVT2OP(fnmadds, { __UNREACHABLE; })

RVT2OP(fadds, { __UNREACHABLE; })

RVT2OP(fsubs, { __UNREACHABLE; })

RVT2OP(fmuls, { __UNREACHABLE; })

RVT2OP(fdivs, { __UNREACHABLE; })

RVT2OP(fsqrts, { __UNREACHABLE; })

RVT2OP(fsgnjs, { __UNREACHABLE; })

RVT2OP(fsgnjns, { __UNREACHABLE; })

RVT2OP(fsgnjxs, { __UNREACHABLE; })

RVT2OP(fmins, { __UNREACHABLE; })

RVT2OP(fmaxs, { __UNREACHABLE; })

RVT2OP(fcvtws, { __UNREACHABLE; })

RVT2OP(fcvtwus, { __UNREACHABLE; })

RVT2OP(fmvxw, { __UNREACHABLE; })

RVT2OP(feqs, { __UNREACHABLE; })

RVT2OP(flts, { __UNREACHABLE; })

RVT2OP(fles, { __UNREACHABLE; })

RVT2OP(fclasss, { __UNREACHABLE; })

RVT2OP(fcvtsw, { __UNREACHABLE; })

RVT2OP(fcvtswu, { __UNREACHABLE; })

RVT2OP(fmvwx, { __UNREACHABLE; })
#endif

#if RV32_HAS(EXT_C)
RVT2OP(caddi4spn, {
    LLVMValueRef val_sp = LLVMBuildLoad2(
        *builder, LLVMInt32Type(), build_sp_addr(start, builder, ir), "val_sp");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt32Type(), (int16_t) ir->imm, true), "add");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})
RVT2OP(clw, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(csw, {
    LLVMValueRef val_rs1 = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
})

RVT2OP(cnop, { return; })

RVT2OP(caddi, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_rd,
        LLVMConstInt(LLVMInt32Type(), (int16_t) ir->imm, true), "add");
    LLVMBuildStore(*builder, res, addr_rd);
})

RVT2OP(cjal, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true),
                   build_ra_addr(start, builder, ir));
    if (ir->branch_taken)
        *taken_builder = *builder;
    else {
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       build_PC_addr(start, builder, ir));
        LLVMBuildRetVoid(*builder);
    }
})

RVT2OP(cli, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   build_rd_addr(start, builder, ir));
})

RVT2OP(caddi16sp, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildAdd(
        *builder, val_rd, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "add");
    LLVMBuildStore(*builder, res, addr_rd);
})

RVT2OP(clui, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   build_rd_addr(start, builder, ir));
})

RVT2OP(csrli, {
    LLVMValueRef addr_rs1 = build_rs1_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res =
        LLVMBuildLShr(*builder, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), ir->shamt, true), "srl");
    LLVMBuildStore(*builder, res, addr_rs1);
})

RVT2OP(csrai, {
    LLVMValueRef addr_rs1 = build_rs1_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res =
        LLVMBuildAShr(*builder, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), ir->shamt, true), "sra");
    LLVMBuildStore(*builder, res, addr_rs1);
})

RVT2OP(candi, {
    LLVMValueRef addr_rs1 = build_rs1_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
    LLVMValueRef res = LLVMBuildAnd(
        *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), ir->imm, true), "and");
    LLVMBuildStore(*builder, res, addr_rs1);
})

RVT2OP(csub, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildSub(*builder, val_rs1, val_rs2, "sub");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(cxor, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildXor(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(cor, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildOr(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(cand, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildAnd(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(cj, {
    if (ir->branch_taken)
        *taken_builder = *builder;
    else {
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       build_PC_addr(start, builder, ir));
        LLVMBuildRetVoid(*builder);
    }
})

RVT2OP(cbeqz, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntEQ, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), 0, true), "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken)
        *taken_builder = builder2;
    else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       build_PC_addr(start, builder, ir));
        LLVMBuildRetVoid(builder2);
    }

    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken)
        *untaken_builder = builder3;
    else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
})

RVT2OP(cbnez, {
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef cond =
        LLVMBuildICmp(*builder, LLVMIntNE, val_rs1,
                      LLVMConstInt(LLVMInt32Type(), 0, true), "cond");
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken)
        *taken_builder = builder2;
    else {
        LLVMBuildStore(builder2,
                       LLVMConstInt(LLVMInt32Type(), ir->pc + ir->imm, true),
                       addr_PC);
        LLVMBuildRetVoid(builder2);
    }

    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken)
        *untaken_builder = builder3;
    else {
        LLVMBuildStore(
            builder3, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true), addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cond, taken, untaken);
})

RVT2OP(cslli, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildShl(
        *builder, val_rd,
        LLVMConstInt(LLVMInt32Type(), (uint8_t) ir->imm, true), "sll");
    LLVMBuildStore(*builder, res, addr_rd);
})

RVT2OP(clwsp, {
    LLVMValueRef val_sp = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_sp_addr(start, builder, ir), "val_sp"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(cjr, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMBuildStore(*builder, val_rs1, addr_PC);
    LLVMBuildRetVoid(*builder);
})

RVT2OP(cmv, {
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMBuildStore(*builder, val_rs2, build_rd_addr(start, builder, ir));
})

RVT2OP(cebreak, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   build_PC_addr(start, builder, ir));
    build_call_io_func(start, builder, param_types, 9);
    LLVMBuildRetVoid(*builder);
})

RVT2OP(cjalr, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc + 2, true),
                   build_ra_addr(start, builder, ir));
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef addr_PC = build_PC_addr(start, builder, ir);
    LLVMBuildStore(*builder, val_rs1, addr_PC);
    LLVMBuildRetVoid(*builder);
})

RVT2OP(cadd, {
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs2_addr(start, builder, ir), "val_rs2");
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, build_rd_addr(start, builder, ir));
})

RVT2OP(cswsp, {
    LLVMValueRef addr_rs2 = build_rs2_addr(start, builder, ir);
    LLVMValueRef val_sp = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_sp_addr(start, builder, ir), "val_sp"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef val_rs2 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs2, "val_rs2");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
})
#endif

#if RV32_HAS(EXT_C) && RV32_HAS(EXT_F)
RVT2OP(cflwsp, { __UNREACHABLE; })

RVT2OP(cfswsp, { __UNREACHABLE; })

RVT2OP(cflw, { __UNREACHABLE; })

RVT2OP(cfsw, { __UNREACHABLE; })
#endif

RVT2OP(fuse1, {
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
})

RVT2OP(fuse2, {
    LLVMValueRef addr_rd = build_rd_addr(start, builder, ir);
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   addr_rd);
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       build_rs1_addr(start, builder, ir), "val_rs1");
    LLVMValueRef val_rd =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rd, "val_rd");
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rd, "add");
    LLVMBuildStore(*builder, res, build_rs2_addr(start, builder, ir));
})

RVT2OP(fuse3, {
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
})

RVT2OP(fuse4, {
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
})

RVT2OP(fuse5, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   build_PC_addr(start, builder, ir));
    build_call_io_func(start, builder, param_types, 10);
    LLVMBuildRetVoid(*builder);
})

RVT2OP(fuse6, {
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->pc, true),
                   build_PC_addr(start, builder, ir));
    build_call_io_func(start, builder, param_types, 11);
    LLVMBuildRetVoid(*builder);
})

RVT2OP(fuse7, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            t2_slli(builder, param_types, start, entry, taken_builder,
                    untaken_builder, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        case rv_insn_srli:
            t2_srli(builder, param_types, start, entry, taken_builder,
                    untaken_builder, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        case rv_insn_srai:
            t2_srai(builder, param_types, start, entry, taken_builder,
                    untaken_builder, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        default:
            __UNREACHABLE;
            break;
        }
    }
})
