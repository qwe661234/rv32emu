// Copyright (c) 2015 Big Switch Networks, Inc
// SPDX-License-Identifier: Apache-2.0

/*
 * Copyright 2015 Big Switch Networks, Inc
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cache.h"
#include "decode.h"
#include "io.h"
#include "state.h"
#include "ubpf_jit_arm64.h"
#include "utils.h"

#define EBPF_CLS_MASK 0x07
#define EBPF_ALU_OP_MASK 0xf0
#define EBPF_JMP_OP_MASK 0xf0

#define EBPF_CLS_LD 0x00
#define EBPF_CLS_LDX 0x01
#define EBPF_CLS_ST 0x02
#define EBPF_CLS_STX 0x03
#define EBPF_CLS_ALU 0x04
#define EBPF_CLS_JMP 0x05
#define EBPF_CLS_JMP32 0x06
#define EBPF_CLS_ALU64 0x07

#define EBPF_SRC_IMM 0x00
#define EBPF_SRC_REG 0x08

#define EBPF_SIZE_W 0x00
#define EBPF_SIZE_H 0x08
#define EBPF_SIZE_B 0x10
#define EBPF_SIZE_DW 0x18

/* Other memory modes are not yet supported */
#define EBPF_MODE_IMM 0x00
#define EBPF_MODE_MEM 0x60

#define EBPF_OP_ADD_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x00)
#define EBPF_OP_ADD_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x00)
#define EBPF_OP_SUB_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x10)
#define EBPF_OP_SUB_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x10)
#define EBPF_OP_MUL_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x20)
#define EBPF_OP_MUL_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x20)
#define EBPF_OP_DIV_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x30)
#define EBPF_OP_DIV_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x30)
#define EBPF_OP_OR_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x40)
#define EBPF_OP_OR_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x40)
#define EBPF_OP_AND_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x50)
#define EBPF_OP_AND_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x50)
#define EBPF_OP_LSH_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x60)
#define EBPF_OP_LSH_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x60)
#define EBPF_OP_RSH_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x70)
#define EBPF_OP_RSH_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x70)
#define EBPF_OP_NEG (EBPF_CLS_ALU | 0x80)
#define EBPF_OP_MOD_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0x90)
#define EBPF_OP_MOD_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0x90)
#define EBPF_OP_XOR_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0xa0)
#define EBPF_OP_XOR_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0xa0)
#define EBPF_OP_MOV_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0xb0)
#define EBPF_OP_MOV_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0xb0)
#define EBPF_OP_ARSH_IMM (EBPF_CLS_ALU | EBPF_SRC_IMM | 0xc0)
#define EBPF_OP_ARSH_REG (EBPF_CLS_ALU | EBPF_SRC_REG | 0xc0)
#define EBPF_OP_LE (EBPF_CLS_ALU | EBPF_SRC_IMM | 0xd0)
#define EBPF_OP_BE (EBPF_CLS_ALU | EBPF_SRC_REG | 0xd0)

#define EBPF_OP_ADD64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x00)
#define EBPF_OP_ADD64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x00)
#define EBPF_OP_SUB64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x10)
#define EBPF_OP_SUB64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x10)
#define EBPF_OP_MUL64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x20)
#define EBPF_OP_MUL64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x20)
#define EBPF_OP_DIV64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x30)
#define EBPF_OP_DIV64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x30)
#define EBPF_OP_OR64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x40)
#define EBPF_OP_OR64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x40)
#define EBPF_OP_AND64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x50)
#define EBPF_OP_AND64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x50)
#define EBPF_OP_LSH64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x60)
#define EBPF_OP_LSH64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x60)
#define EBPF_OP_RSH64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x70)
#define EBPF_OP_RSH64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x70)
#define EBPF_OP_NEG64 (EBPF_CLS_ALU64 | 0x80)
#define EBPF_OP_MOD64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0x90)
#define EBPF_OP_MOD64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0x90)
#define EBPF_OP_XOR64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0xa0)
#define EBPF_OP_XOR64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0xa0)
#define EBPF_OP_MOV64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0xb0)
#define EBPF_OP_MOV64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0xb0)
#define EBPF_OP_ARSH64_IMM (EBPF_CLS_ALU64 | EBPF_SRC_IMM | 0xc0)
#define EBPF_OP_ARSH64_REG (EBPF_CLS_ALU64 | EBPF_SRC_REG | 0xc0)

#define EBPF_OP_LDXW (EBPF_CLS_LDX | EBPF_MODE_MEM | EBPF_SIZE_W)
#define EBPF_OP_LDXH (EBPF_CLS_LDX | EBPF_MODE_MEM | EBPF_SIZE_H)
#define EBPF_OP_LDXB (EBPF_CLS_LDX | EBPF_MODE_MEM | EBPF_SIZE_B)
#define EBPF_OP_LDXDW (EBPF_CLS_LDX | EBPF_MODE_MEM | EBPF_SIZE_DW)
#define EBPF_OP_STW (EBPF_CLS_ST | EBPF_MODE_MEM | EBPF_SIZE_W)
#define EBPF_OP_STH (EBPF_CLS_ST | EBPF_MODE_MEM | EBPF_SIZE_H)
#define EBPF_OP_STB (EBPF_CLS_ST | EBPF_MODE_MEM | EBPF_SIZE_B)
#define EBPF_OP_STDW (EBPF_CLS_ST | EBPF_MODE_MEM | EBPF_SIZE_DW)
#define EBPF_OP_STXW (EBPF_CLS_STX | EBPF_MODE_MEM | EBPF_SIZE_W)
#define EBPF_OP_STXH (EBPF_CLS_STX | EBPF_MODE_MEM | EBPF_SIZE_H)
#define EBPF_OP_STXB (EBPF_CLS_STX | EBPF_MODE_MEM | EBPF_SIZE_B)
#define EBPF_OP_STXDW (EBPF_CLS_STX | EBPF_MODE_MEM | EBPF_SIZE_DW)
#define EBPF_OP_LDDW (EBPF_CLS_LD | EBPF_MODE_IMM | EBPF_SIZE_DW)

#define EBPF_MODE_JA 0x00
#define EBPF_MODE_JEQ 0x10
#define EBPF_MODE_JGT 0x20
#define EBPF_MODE_JGE 0x30
#define EBPF_MODE_JSET 0x40
#define EBPF_MODE_JNE 0x50
#define EBPF_MODE_JSGT 0x60
#define EBPF_MODE_JSGE 0x70
#define EBPF_MODE_CALL 0x80
#define EBPF_MODE_EXIT 0x90
#define EBPF_MODE_JLT 0xa0
#define EBPF_MODE_JLE 0xb0
#define EBPF_MODE_JSLT 0xc0
#define EBPF_MODE_JSLE 0xd0

#define EBPF_OP_JA (EBPF_CLS_JMP | EBPF_MODE_JA)
#define EBPF_OP_JEQ_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JEQ)
#define EBPF_OP_JEQ_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JEQ)
#define EBPF_OP_JGT_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JGT)
#define EBPF_OP_JGT_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JGT)
#define EBPF_OP_JGE_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JGE)
#define EBPF_OP_JGE_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JGE)
#define EBPF_OP_JSET_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JSET)
#define EBPF_OP_JSET_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JSET)
#define EBPF_OP_JNE_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JNE)
#define EBPF_OP_JNE_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JNE)
#define EBPF_OP_JSGT_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JSGT)
#define EBPF_OP_JSGT_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JSGT)
#define EBPF_OP_JSGE_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JSGE)
#define EBPF_OP_JSGE_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JSGE)
#define EBPF_OP_CALL (EBPF_CLS_JMP | EBPF_MODE_CALL)
#define EBPF_OP_EXIT (EBPF_CLS_JMP | EBPF_MODE_EXIT)
#define EBPF_OP_JLT_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JLT)
#define EBPF_OP_JLT_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JLT)
#define EBPF_OP_JLE_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JLE)
#define EBPF_OP_JLE_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JLE)
#define EBPF_OP_JSLT_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JSLT)
#define EBPF_OP_JSLT_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JSLT)
#define EBPF_OP_JSLE_IMM (EBPF_CLS_JMP | EBPF_SRC_IMM | EBPF_MODE_JSLE)
#define EBPF_OP_JSLE_REG (EBPF_CLS_JMP | EBPF_SRC_REG | EBPF_MODE_JSLE)

#define EBPF_OP_JEQ32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JEQ)
#define EBPF_OP_JEQ32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JEQ)
#define EBPF_OP_JGT32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JGT)
#define EBPF_OP_JGT32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JGT)
#define EBPF_OP_JGE32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JGE)
#define EBPF_OP_JGE32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JGE)
#define EBPF_OP_JSET32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JSET)
#define EBPF_OP_JSET32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JSET)
#define EBPF_OP_JNE32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JNE)
#define EBPF_OP_JNE32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JNE)
#define EBPF_OP_JSGT32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JSGT)
#define EBPF_OP_JSGT32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JSGT)
#define EBPF_OP_JSGE32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JSGE)
#define EBPF_OP_JSGE32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JSGE)
#define EBPF_OP_JLT32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JLT)
#define EBPF_OP_JLT32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JLT)
#define EBPF_OP_JLE32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JLE)
#define EBPF_OP_JLE32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JLE)
#define EBPF_OP_JSLT32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JSLT)
#define EBPF_OP_JSLT32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JSLT)
#define EBPF_OP_JSLE32_IMM (EBPF_CLS_JMP32 | EBPF_SRC_IMM | EBPF_MODE_JSLE)
#define EBPF_OP_JSLE32_REG (EBPF_CLS_JMP32 | EBPF_SRC_REG | EBPF_MODE_JSLE)

enum bpf_register {
    BPF_REG_0 = 0,
    BPF_REG_1,
    BPF_REG_2,
    BPF_REG_3,
    BPF_REG_4,
    BPF_REG_5,
    BPF_REG_6,
    BPF_REG_7,
    BPF_REG_8,
    BPF_REG_9,
    BPF_REG_10,
    _BPF_REG_MAX,
};

#define UBPF_STACK_SIZE 512
#define UBPF_MAX_INSTS 65536

#if !defined(_countof)
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

#define REGISTER_MAP_SIZE 11

/*
 * There are two common x86-64 calling conventions, as discussed at
 * https://en.wikipedia.org/wiki/X86_calling_conventions#x86-64_calling_conventions
 *
 * Please Note: R12 is special and we are *not* using it. As a result, it is
 * omitted from the list of non-volatile registers for both platforms (even
 * though it is, in fact, non-volatile).
 *
 * BPF R0-R4 are "volatile"
 * BPF R5-R10 are "non-volatile"
 * In general, we attempt to map BPF volatile registers to x64 volatile and BPF
 * non- volatile to x64 non-volatile.
 */

// Callee saved registers - this must be a multiple of two because of how we
// save the stack later on.
static enum Registers callee_saved_registers[] = {R19, R20, R21, R22,
                                                  R23, R24, R25, R26};
// Caller saved registers (and parameter registers)
static int platform_parameter_registers[] = {R0, R1, R2, R3, R4};
// Temp register for immediate generation
static enum Registers temp_register = R24;
// Temp register for division results
static enum Registers temp_div_register = R25;
// Temp register for load/store offsets
static enum Registers offset_register = R26;

// Register assignments:
//   BPF        Arm64       Usage
//   r0         r5          Return value from calls (see note)
//   r1 - r5    r0 - r4     Function parameters, caller-saved
//   r6 - r10   r19 - r23   Callee-saved registers
//              r24         Temp - used for generating 32-bit immediates
//              r25         Temp - used for modulous calculations
//              r26         Temp - used for large load/store offsets
//
// Note that the AArch64 ABI uses r0 both for function parameters and result. We
// use r5 to hold the result during the function and do an extra final move at
// the end of the function to copy the result to the correct place.
static enum Registers register_map[REGISTER_MAP_SIZE] = {
    R5,  // result
    R0,  R1,  R2,  R3,
    R4,  // parameters
    R19, R20, R21, R22,
    R23,  // callee-saved
};

/* Return the x86 register for the given eBPF register */
static int map_register(int r)
{
    assert(r < _BPF_REG_MAX);
    return register_map[r % _BPF_REG_MAX];
}

/* Some forward declarations.  */
static inline void emit_movewide_immediate(struct jit_state *state,
                                           bool sixty_four,
                                           enum Registers rd,
                                           uint64_t imm);
static void divmod(struct jit_state *state,
                   uint8_t opcode,
                   int rd,
                   int rn,
                   int rm);

static uint32_t inline align_to(uint32_t amount, uint64_t boundary)
{
    return (amount + (boundary - 1)) & ~(boundary - 1);
}

#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *) (addr), (char *) (addr) + (size));

static inline void emit_bytes(struct jit_state *state, void *data, uint32_t len)
{
    assert(state->offset <= state->size - len);
    if ((state->offset + len) > state->size) {
        state->offset = state->size;
        return;
    }
    memcpy(state->buf + state->offset, data, len);
    sys_icache_invalidate(state->buf + state->offset, len);
    state->offset += len;
}



static void emit_instruction(struct jit_state *state, uint32_t instr)
{
    assert(instr != BAD_OPCODE);
    // printf("%2x", instr & 0x000000FF);
    // printf("%2x", (instr & 0x0000FF00) >> 8);
    // printf("%2x", (instr & 0x00FF0000) >> 16);
    // printf("%2x\n", (instr & 0xFF000000) >> 24);
    emit_bytes(state, &instr, 4);
}

enum AddSubOpcode { AS_ADD = 0, AS_ADDS = 1, AS_SUB = 2, AS_SUBS = 3 };

/* Get the value of the size bit in most instruction encodings (bit 31). */
static uint32_t sz(bool sixty_four)
{
    return (sixty_four ? UINT32_C(1) : UINT32_C(0)) << 31;
}

/* [ArmARM-A H.a]: C4.1.64: Add/subtract (immediate).  */
static void emit_addsub_immediate(struct jit_state *state,
                                  bool sixty_four,
                                  enum AddSubOpcode op,
                                  enum Registers rd,
                                  enum Registers rn,
                                  uint32_t imm12)
{
    const uint32_t imm_op_base = 0x11000000;
    emit_instruction(state, sz(sixty_four) | (op << 29) | imm_op_base |
                                (0 << 22) | (imm12 << 10) | (rn << 5) | rd);
}


enum LogicalOpcode {
    //  op         N
    LOG_AND = 0x00000000U,   // 0000_0000_0000_0000_0000_0000_0000_0000
    LOG_BIC = 0x00200000U,   // 0000_0000_0010_0000_0000_0000_0000_0000
    LOG_ORR = 0x20000000U,   // 0010_0000_0000_0000_0000_0000_0000_0000
    LOG_ORN = 0x20200000U,   // 0010_0000_0010_0000_0000_0000_0000_0000
    LOG_EOR = 0x40000000U,   // 0100_0000_0000_0000_0000_0000_0000_0000
    LOG_EON = 0x40200000U,   // 0100_0000_0010_0000_0000_0000_0000_0000
    LOG_ANDS = 0x60000000U,  // 0110_0000_0000_0000_0000_0000_0000_0000
    LOG_BICS = 0x60200000U,  // 0110_0000_0010_0000_0000_0000_0000_0000
};


/* [ArmARM-A H.a]: C4.1.67: Logical (shifted register).  */
static void emit_logical_register(struct jit_state *state,
                                  bool sixty_four,
                                  enum LogicalOpcode op,
                                  enum Registers rd,
                                  enum Registers rn,
                                  enum Registers rm)
{
    emit_instruction(state, sz(sixty_four) | op | (1 << 27) | (1 << 25) |
                                (rm << 16) | (rn << 5) | rd);
}

/* [ArmARM-A H.a]: C4.1.67: Add/subtract (shifted register).  */
static inline void emit_addsub_register(struct jit_state *state,
                                        bool sixty_four,
                                        enum AddSubOpcode op,
                                        enum Registers rd,
                                        enum Registers rn,
                                        enum Registers rm)
{
    const uint32_t reg_op_base = 0x0b000000;
    emit_instruction(state, sz(sixty_four) | (op << 29) | reg_op_base |
                                (rm << 16) | (rn << 5) | rd);
}

static inline void emit_load_imm(struct jit_state *state, int dst, int64_t imm);

static inline void emit_alu32_imm32(struct jit_state *state,
                                    int op,
                                    int src,
                                    int dst,
                                    int32_t imm)
{
    switch (src) {
    case 0:
        emit_load_imm(state, R10, imm);
        emit_addsub_register(state, false, AS_ADD, dst, dst, R10);
        break;
    case 1:
        emit_load_imm(state, R10, imm);
        emit_logical_register(state, false, LOG_ORR, dst, dst, R10);
        break;
    case 4:
        emit_load_imm(state, R10, imm);
        emit_logical_register(state, false, LOG_AND, dst, dst, R10);
        break;
    case 6:
        emit_load_imm(state, R10, imm);
        emit_logical_register(state, false, LOG_EOR, dst, src, R10);
        break;
    }
}

enum LoadStoreOpcode {
    // sz    V   op
    LS_STRB = 0x00000000U,    // 0000_0000_0000_0000_0000_0000_0000_0000
    LS_LDRB = 0x00400000U,    // 0000_0000_0100_0000_0000_0000_0000_0000
    LS_LDRSBX = 0x00800000U,  // 0000_0000_1000_0000_0000_0000_0000_0000
    LS_LDRSBW = 0x00c00000U,  // 0000_0000_1100_0000_0000_0000_0000_0000
    LS_STRH = 0x40000000U,    // 0100_0000_0000_0000_0000_0000_0000_0000
    LS_LDRH = 0x40400000U,    // 0100_0000_0100_0000_0000_0000_0000_0000
    LS_LDRSHX = 0x40800000U,  // 0100_0000_1000_0000_0000_0000_0000_0000
    LS_LDRSHW = 0x40c00000U,  // 0100_0000_1100_0000_0000_0000_0000_0000
    LS_STRW = 0x80000000U,    // 1000_0000_0000_0000_0000_0000_0000_0000
    LS_LDRW = 0x80400000U,    // 1000_0000_0100_0000_0000_0000_0000_0000
    LS_LDRSW = 0x80800000U,   // 1000_0000_1000_0000_0000_0000_0000_0000
    LS_STRX = 0xc0000000U,    // 1100_0000_0000_0000_0000_0000_0000_0000
    LS_LDRX = 0xc0400000U,    // 1100_0000_0100_0000_0000_0000_0000_0000
};

/* [ArmARM-A H.a]: C4.1.66: Load/store register (unscaled immediate).  */
static void emit_loadstore_immediate(struct jit_state *state,
                                     enum LoadStoreOpcode op,
                                     enum Registers rt,
                                     enum Registers rn,
                                     int16_t imm9)
{
    const uint32_t imm_op_base = 0x38000000U;
    assert(imm9 >= -256 && imm9 < 256);
    imm9 &= 0x1ff;
    emit_instruction(state, imm_op_base | op | (imm9 << 12) | (rn << 5) | rt);
}

static inline void emit_load(struct jit_state *state,
                             enum operand_size size,
                             int src,
                             int dst,
                             int32_t offset)
{
    if (size == S8)
        emit_loadstore_immediate(state, LS_LDRB, dst, src, offset);
    else if (size == S16)
        emit_loadstore_immediate(state, LS_LDRH, dst, src, offset);
    else if (size == S32)
        emit_loadstore_immediate(state, LS_LDRW, dst, src, offset);
    else if (size == S64)
        emit_loadstore_immediate(state, LS_LDRX, dst, src, offset);
}

static inline void emit_load_sext(struct jit_state *state,
                                  enum operand_size size,
                                  int src,
                                  int dst,
                                  int32_t offset)
{
    if (size == S8)
        emit_loadstore_immediate(state, LS_LDRSBW, dst, src, offset);
    else if (size == S16)
        emit_loadstore_immediate(state, LS_LDRSHW, dst, src, offset);
}

static inline void emit_store(struct jit_state *state,
                              enum operand_size size,
                              int src,
                              int dst,
                              int32_t offset)
{
    if (size == S8)
        emit_loadstore_immediate(state, LS_STRB, src, dst, offset);
    else if (size == S16)
        emit_loadstore_immediate(state, LS_STRH, src, dst, offset);
    else if (size == S32)
        emit_loadstore_immediate(state, LS_STRW, src, dst, offset);
    else if (size == S64)
        emit_loadstore_immediate(state, LS_STRX, src, dst, offset);
}

/* [ArmARM-A H.a]: C4.1.66: Load/store register (register offset).  */
static void emit_loadstore_register(struct jit_state *state,
                                    enum LoadStoreOpcode op,
                                    enum Registers rt,
                                    enum Registers rn,
                                    enum Registers rm)
{
    const uint32_t reg_op_base = 0x38206800U;
    emit_instruction(state, op | reg_op_base | (rm << 16) | (rn << 5) | rt);
}

enum LoadStorePairOpcode {
    // op    V    L
    LSP_STPW = 0x29000000U,   // 0010_1001_0000_0000_0000_0000_0000_0000
    LSP_LDPW = 0x29400000U,   // 0010_1001_0100_0000_0000_0000_0000_0000
    LSP_LDPSW = 0x69400000U,  // 0110_1001_0100_0000_0000_0000_0000_0000
    LSP_STPX = 0xa9000000U,   // 1010_1001_0000_0000_0000_0000_0000_0000
    LSP_LDPX = 0xa9400000U,   // 1010_1001_0100_0000_0000_0000_0000_0000
};

/* [ArmARM-A H.a]: C4.1.66: Load/store register pair (offset).  */
static void emit_loadstorepair_immediate(struct jit_state *state,
                                         enum LoadStorePairOpcode op,
                                         enum Registers rt,
                                         enum Registers rt2,
                                         enum Registers rn,
                                         int32_t imm7)
{
    int32_t imm_div = ((op == LSP_STPX) || (op == LSP_LDPX)) ? 8 : 4;
    assert(imm7 % imm_div == 0);
    imm7 /= imm_div;
    emit_instruction(state, op | (imm7 << 15) | (rt2 << 10) | (rn << 5) | rt);
}

enum UnconditionalBranchOpcode {
    //         opc-|op2--|op3----|        op4|
    BR_BR = 0xd61f0000U,   // 1101_0110_0001_1111_0000_0000_0000_0000
    BR_BLR = 0xd63f0000U,  // 1101_0110_0011_1111_0000_0000_0000_0000
    BR_RET = 0xd65f0000U,  // 1101_0110_0101_1111_0000_0000_0000_0000
};

/* [ArmARM-A H.a]: C4.1.65: Unconditional branch (register).  */
static void emit_unconditionalbranch_register(struct jit_state *state,
                                              enum UnconditionalBranchOpcode op,
                                              enum Registers rn)
{
    emit_instruction(state, op | (rn << 5));
}

enum UnconditionalBranchImmediateOpcode {
    // O
    UBR_B = 0x14000000U,   // 0001_0100_0000_0000_0000_0000_0000_0000
    UBR_BL = 0x94000000U,  // 1001_0100_0000_0000_0000_0000_0000_0000
};

static void note_jump(struct jit_state *state, uint32_t target_pc)
{
    if (state->num_jumps == UBPF_MAX_INSTS) {
        return;
    }
    struct jump *jump = &state->jumps[state->num_jumps++];
    jump->offset_loc = state->offset;
    jump->target_pc = target_pc;
}

// /* [ArmARM-A H.a]: C4.1.65: Unconditional branch (immediate).  */
// static inline void emit_unconditionalbranch_immediate(
//     struct jit_state *state,
//     enum UnconditionalBranchImmediateOpcode op,
//     int32_t target_pc)
// {
//     note_jump(state, target_pc);
//     emit_instruction(state, op);
// }

static inline void emit_jmp(struct jit_state *state, uint32_t target_pc)
{
    note_jump(state, target_pc);
    emit_instruction(state, UBR_B);
}

static inline void emit_exit(struct jit_state *state)
{
    emit_jmp(state, TARGET_PC_EXIT);
}

enum Condition {
    COND_EQ,
    COND_NE,
    COND_CS,
    COND_CC,
    COND_MI,
    COND_PL,
    COND_VS,
    COND_VC,
    COND_HI,
    COND_LS,
    COND_GE,
    COND_LT,
    COND_GT,
    COND_LE,
    COND_AL,
    COND_NV,
    COND_HS = COND_CS,
    COND_LO = COND_CC
};

enum ConditionalBranchImmediateOpcode { BR_Bcond = 0x54000000U };

/* [ArmARM-A H.a]: C4.1.65: Conditional branch (immediate).  */
static void emit_conditionalbranch_immediate(struct jit_state *state,
                                             enum Condition cond,
                                             uint32_t target_pc)
{
    note_jump(state, target_pc);
    emit_instruction(state, BR_Bcond | (0 << 5) | cond);
}

enum CompareBranchOpcode {
    //          o
    CBR_CBZ = 0x34000000U,   // 0011_0100_0000_0000_0000_0000_0000_0000
    CBR_CBNZ = 0x35000000U,  // 0011_0101_0000_0000_0000_0000_0000_0000
};

#if 0
static void
emit_comparebranch_immediate(struct jit_state *state, bool sixty_four, enum CompareBranchOpcode op, enum Registers rt, uint32_t target_pc)
{
    note_jump(state, target_pc);
    emit_instruction(state, (sixty_four << 31) | op | rt);
}
#endif

enum DP1Opcode {
    //   S          op2--|op-----|
    DP1_REV16 = 0x5ac00400U,  // 0101_1010_1100_0000_0000_0100_0000_0000
    DP1_REV32 = 0x5ac00800U,  // 0101_1010_1100_0000_0000_1000_0000_0000
    DP1_REV64 = 0xdac00c00U,  // 0101_1010_1100_0000_0000_1100_0000_0000
};

/* [ArmARM-A H.a]: C4.1.67: Data-processing (1 source).  */
static void emit_dataprocessing_onesource(struct jit_state *state,
                                          bool sixty_four,
                                          enum DP1Opcode op,
                                          enum Registers rd,
                                          enum Registers rn)
{
    emit_instruction(state, sz(sixty_four) | op | (rn << 5) | rd);
}

enum DP2Opcode {
    //   S                 opcode|
    DP2_UDIV = 0x1ac00800U,  // 0001_1010_1100_0000_0000_1000_0000_0000
    DP2_SDIV = 0x1ac00c00U,  // 0001_1010_1100_0000_0000_1100_0000_0000
    DP2_LSLV = 0x1ac02000U,  // 0001_1010_1100_0000_0010_0000_0000_0000
    DP2_LSRV = 0x1ac02400U,  // 0001_1010_1100_0000_0010_0100_0000_0000
    DP2_ASRV = 0x1ac02800U,  // 0001_1010_1100_0000_0010_1000_0000_0000
    DP2_RORV = 0x1ac02800U,  // 0001_1010_1100_0000_0010_1100_0000_0000
};

/* [ArmARM-A H.a]: C4.1.67: Data-processing (2 source).  */
static void emit_dataprocessing_twosource(struct jit_state *state,
                                          bool sixty_four,
                                          enum DP2Opcode op,
                                          enum Registers rd,
                                          enum Registers rn,
                                          enum Registers rm)
{
    emit_instruction(state, sz(sixty_four) | op | (rm << 16) | (rn << 5) | rd);
}


static inline void emit_alu32(struct jit_state *state, int op, int src, int dst)
{
    switch (op) {
    case 1: /* ADD */
        emit_addsub_register(state, false, AS_ADD, dst, dst, src);
        break;
    case 0x29: /* SUB */
        emit_addsub_register(state, false, AS_SUB, dst, dst, src);
        break;
    case 0x31: /* XOR */
        emit_logical_register(state, false, LOG_EOR, dst, dst, src);
        break;
    case 9: /* OR */
        emit_logical_register(state, false, LOG_ORR, dst, dst, src);
        break;
    case 0x21: /* AND */
        emit_logical_register(state, false, LOG_AND, dst, dst, src);
        break;
    case 0xd3:
        if (src == 4) /* SLL */
            emit_dataprocessing_twosource(state, false, DP2_LSLV, dst, dst, R8);
        else if (src == 5) /* SRL */
            emit_dataprocessing_twosource(state, false, DP2_LSRV, dst, dst, R8);
        else if (src == 7) /* SRA */
            emit_dataprocessing_twosource(state, false, DP2_ASRV, dst, dst, R8);
        break;
    }
}

static inline void emit_alu64(struct jit_state *state, int op, int src, int dst)
{
    if (op == 0x01)
        emit_addsub_register(state, true, AS_ADD, dst, dst, src);
}

static inline void emit_alu64_imm8(struct jit_state *state,
                                   int op,
                                   int src,
                                   int dst,
                                   int8_t imm)
{
    if (op == 0xc1) {
        emit_load_imm(state, R10, imm);
        emit_dataprocessing_twosource(state, true, DP2_LSRV, dst, dst, R10);
    }
}

static inline void emit_alu32_imm8(struct jit_state *state,
                                   int op,
                                   int src,
                                   int dst,
                                   int32_t imm)
{
    switch (src) {
    case 4:
        emit_load_imm(state, R10, imm);
        emit_dataprocessing_twosource(state, false, DP2_LSLV, dst, dst, R10);
        break;
    case 5:
        emit_load_imm(state, R10, imm);
        emit_dataprocessing_twosource(state, false, DP2_LSRV, dst, dst, R10);
        break;
    case 7:
        emit_load_imm(state, R10, imm);
        emit_dataprocessing_twosource(state, false, DP2_ASRV, dst, dst, R10);
        break;
    }
}

enum DP3Opcode {
    //  54       31|       0
    DP3_MADD = 0x1b000000U,  // 0001_1011_0000_0000_0000_0000_0000_0000
    DP3_MSUB = 0x1b008000U,  // 0001_1011_0000_0000_1000_0000_0000_0000
};

/* [ArmARM-A H.a]: C4.1.67: Data-processing (3 source).  */
static void emit_dataprocessing_threesource(struct jit_state *state,
                                            bool sixty_four,
                                            enum DP3Opcode op,
                                            enum Registers rd,
                                            enum Registers rn,
                                            enum Registers rm,
                                            enum Registers ra)
{
    emit_instruction(
        state, sz(sixty_four) | op | (rm << 16) | (ra << 10) | (rn << 5) | rd);
}

enum MoveWideOpcode {
    //  op
    MW_MOVN = 0x12800000U,  // 0001_0010_1000_0000_0000_0000_0000_0000
    MW_MOVZ = 0x52800000U,  // 0101_0010_1000_0000_0000_0000_0000_0000
    MW_MOVK = 0x72800000U,  // 0111_0010_1000_0000_0000_0000_0000_0000
};

/* [ArmARM-A H.a]: C4.1.64: Move wide (Immediate).  */
static inline void emit_movewide_immediate(struct jit_state *state,
                                           bool sixty_four,
                                           enum Registers rd,
                                           uint64_t imm)
{
    /* Emit a MOVZ or MOVN followed by a sequence of MOVKs to generate the
     * 64-bit constant in imm. See whether the 0x0000 or 0xffff pattern is more
     * common in the immediate.  This ensures we produce the fewest number of
     * immediates.
     */
    unsigned count0000 = sixty_four ? 0 : 2;
    unsigned countffff = 0;
    for (unsigned i = 0; i < (sixty_four ? 64 : 32); i += 16) {
        uint64_t block = (imm >> i) & 0xffff;
        if (block == 0xffff) {
            ++countffff;
        } else if (block == 0) {
            ++count0000;
        }
    }

    /* Iterate over 16-bit elements of imm, outputting an appropriate move
     * instruction.  */
    bool invert = (count0000 < countffff);
    enum MoveWideOpcode op = invert ? MW_MOVN : MW_MOVZ;
    uint64_t skip_pattern = invert ? 0xffff : 0;
    for (unsigned i = 0; i < (sixty_four ? 4 : 2); ++i) {
        uint64_t imm16 = (imm >> (i * 16)) & 0xffff;
        if (imm16 != skip_pattern) {
            if (invert) {
                imm16 = ~imm16;
                imm16 &= 0xffff;
            }
            emit_instruction(
                state, sz(sixty_four) | op | (i << 21) | (imm16 << 5) | rd);
            op = MW_MOVK;
            invert = false;
        }
    }

    /* Tidy up for the case imm = 0 or imm == -1.  */
    if (op != MW_MOVK) {
        emit_instruction(state,
                         sz(sixty_four) | op | (0 << 21) | (0 << 5) | rd);
    }
}

/* Load sign-extended immediate into register */
static inline void emit_load_imm(struct jit_state *state, int dst, int64_t imm)
{
    if (imm >= INT32_MIN && imm <= INT32_MAX)
        emit_movewide_immediate(state, false, dst, imm);
    else
        emit_movewide_immediate(state, true, dst, imm);
}

static void update_branch_immediate(struct jit_state *state,
                                    uint32_t offset,
                                    int32_t imm)
{
    assert((imm & 3) == 0);
    uint32_t instr;
    imm >>= 2;
    memcpy(&instr, state->buf + offset, sizeof(uint32_t));
    if ((instr & 0xfe000000U) ==
            0x54000000U /* Conditional branch immediate.  */
        || (instr & 0x7e000000U) ==
               0x34000000U) { /* Compare and branch immediate.  */
        assert((imm >> 19) == INT64_C(-1) || (imm >> 19) == 0);
        instr |= (imm & 0x7ffff) << 5;
    } else if ((instr & 0x7c000000U) == 0x14000000U) {
        /* Unconditional branch immediate.  */
        assert((imm >> 26) == INT64_C(-1) || (imm >> 26) == 0);
        instr |= (imm & 0x03ffffffU) << 0;
    } else {
        assert(false);
        instr = BAD_OPCODE;
    }
    memcpy(state->buf + offset, &instr, sizeof(uint32_t));
}

#define SET_SIZE_BITS 10
#define SET_SIZE 1 << SET_SIZE_BITS
#define SET_SLOTS_SIZE 32
HASH_FUNC_IMPL(set_hash, SET_SIZE_BITS, 1 << SET_SIZE_BITS);

/*
 * The set consists of SET_SIZE buckets, with each bucket containing
 * SET_SLOTS_SIZE slots.
 */
typedef struct {
    uint32_t table[SET_SIZE][SET_SLOTS_SIZE];
} set_t;

/**
 * set_reset - clear a set
 * @set: a pointer points to target set
 */
static inline void set_reset(set_t *set)
{
    memset(set, 0, sizeof(set_t));
}

/**
 * set_add - insert a new element into the set
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_add(set_t *set, uint32_t key)
{
    const uint32_t index = set_hash(key);
    uint8_t count = 0;
    while (set->table[index][count]) {
        if (set->table[index][count++] == key)
            return false;
    }

    set->table[index][count] = key;
    return true;
}

/**
 * set_has - check whether the element exist in the set or not
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_has(set_t *set, uint32_t key)
{
    const uint32_t index = set_hash(key);
    for (uint8_t count = 0; set->table[index][count]; count++) {
        if (set->table[index][count] == key)
            return true;
    }
    return false;
}

#define UPDATE_PC(pc)                                           \
    emit_load_imm(state, R6, (pc));                             \
    emit_store(state, S32, R6, platform_parameter_registers[0], \
               offsetof(struct riscv_internal, PC));

static bool is_imm_op(struct ebpf_inst const *inst)
{
    // int class = inst->opcode & EBPF_CLS_MASK;
    // bool is_imm = (inst->opcode & EBPF_SRC_REG) == EBPF_SRC_IMM;
    // bool is_endian = (inst->opcode & EBPF_ALU_OP_MASK) == 0xd0;
    // bool is_neg = (inst->opcode & EBPF_ALU_OP_MASK) == 0x80;
    // bool is_call = inst->opcode == EBPF_OP_CALL;
    // bool is_exit = inst->opcode == EBPF_OP_EXIT;
    // bool is_ja = inst->opcode == EBPF_OP_JA;
    // bool is_alu = (class == EBPF_CLS_ALU || class == EBPF_CLS_ALU64) &&
    // !is_endian && !is_neg; bool is_jmp = (class == EBPF_CLS_JMP && !is_ja &&
    // !is_call && !is_exit); bool is_jmp32 = class == EBPF_CLS_JMP32; bool
    // is_store = class == EBPF_CLS_ST; return (is_imm && (is_alu || is_jmp ||
    // is_jmp32)) || is_store;
    return false;
}

static bool is_alu64_op(struct ebpf_inst const *inst)
{
    // int class = inst->opcode & EBPF_CLS_MASK;
    // return class == EBPF_CLS_ALU64 || class == EBPF_CLS_JMP;
    return true;
}

static bool is_simple_imm(struct ebpf_inst const *inst)
{
    // switch (inst->opcode) {
    // case EBPF_OP_ADD_IMM:
    // case EBPF_OP_ADD64_IMM:
    // case EBPF_OP_SUB_IMM:
    // case EBPF_OP_SUB64_IMM:
    // case EBPF_OP_JEQ_IMM:
    // case EBPF_OP_JGT_IMM:
    // case EBPF_OP_JGE_IMM:
    // case EBPF_OP_JNE_IMM:
    // case EBPF_OP_JSGT_IMM:
    // case EBPF_OP_JSGE_IMM:
    // case EBPF_OP_JLT_IMM:
    // case EBPF_OP_JLE_IMM:
    // case EBPF_OP_JSLT_IMM:
    // case EBPF_OP_JSLE_IMM:
    // case EBPF_OP_JEQ32_IMM:
    // case EBPF_OP_JGT32_IMM:
    // case EBPF_OP_JGE32_IMM:
    // case EBPF_OP_JNE32_IMM:
    // case EBPF_OP_JSGT32_IMM:
    // case EBPF_OP_JSGE32_IMM:
    // case EBPF_OP_JLT32_IMM:
    // case EBPF_OP_JLE32_IMM:
    // case EBPF_OP_JSLT32_IMM:
    // case EBPF_OP_JSLE32_IMM:
    //     return inst->imm >= 0 && inst->imm < 0x1000;
    // case EBPF_OP_MOV_IMM:
    // case EBPF_OP_MOV64_IMM:
    //     return true;
    // case EBPF_OP_AND_IMM:
    // case EBPF_OP_AND64_IMM:
    // case EBPF_OP_OR_IMM:
    // case EBPF_OP_OR64_IMM:
    // case EBPF_OP_XOR_IMM:
    // case EBPF_OP_XOR64_IMM:
    //     return false;
    // case EBPF_OP_ARSH_IMM:
    // case EBPF_OP_ARSH64_IMM:
    // case EBPF_OP_LSH_IMM:
    // case EBPF_OP_LSH64_IMM:
    // case EBPF_OP_RSH_IMM:
    // case EBPF_OP_RSH64_IMM:
    //     return false;
    // case EBPF_OP_JSET_IMM:
    // case EBPF_OP_JSET32_IMM:
    //     return false;
    // case EBPF_OP_DIV_IMM:
    // case EBPF_OP_DIV64_IMM:
    // case EBPF_OP_MOD_IMM:
    // case EBPF_OP_MOD64_IMM:
    // case EBPF_OP_MUL_IMM:
    // case EBPF_OP_MUL64_IMM:
    //     return false;
    // case EBPF_OP_STB:
    // case EBPF_OP_STH:
    // case EBPF_OP_STW:
    // case EBPF_OP_STDW:
    //     return false;
    // default:
    //     assert(false);
    //     return false;
    // }
    return false;
}


static uint8_t to_reg_op(uint8_t opcode)
{
    int class = opcode & EBPF_CLS_MASK;
    if (class == EBPF_CLS_ALU64 || class == EBPF_CLS_ALU ||
        class == EBPF_CLS_JMP || class == EBPF_CLS_JMP32) {
        return opcode | EBPF_SRC_REG;
    } else if (class == EBPF_CLS_ST) {
        return (opcode & ~EBPF_CLS_MASK) | EBPF_CLS_STX;
    }
    assert(false);
    return 0;
}

static enum AddSubOpcode to_addsub_opcode(int opcode)
{
    switch (opcode) {
    case EBPF_OP_ADD_IMM:
    case EBPF_OP_ADD_REG:
    case EBPF_OP_ADD64_IMM:
    case EBPF_OP_ADD64_REG:
        return AS_ADD;
    case EBPF_OP_SUB_IMM:
    case EBPF_OP_SUB_REG:
    case EBPF_OP_SUB64_IMM:
    case EBPF_OP_SUB64_REG:
        return AS_SUB;
    default:
        assert(false);
        return (enum AddSubOpcode) BAD_OPCODE;
    }
}

static enum LogicalOpcode to_logical_opcode(int opcode)
{
    switch (opcode) {
    case EBPF_OP_OR_IMM:
    case EBPF_OP_OR_REG:
    case EBPF_OP_OR64_IMM:
    case EBPF_OP_OR64_REG:
        return LOG_ORR;
    case EBPF_OP_AND_IMM:
    case EBPF_OP_AND_REG:
    case EBPF_OP_AND64_IMM:
    case EBPF_OP_AND64_REG:
        return LOG_AND;
    case EBPF_OP_XOR_IMM:
    case EBPF_OP_XOR_REG:
    case EBPF_OP_XOR64_IMM:
    case EBPF_OP_XOR64_REG:
        return LOG_EOR;
    default:
        assert(false);
        return (enum LogicalOpcode) BAD_OPCODE;
    }
}

static enum DP1Opcode to_dp1_opcode(int opcode, uint32_t imm)
{
    switch (opcode) {
    case EBPF_OP_BE:
    case EBPF_OP_LE:
        switch (imm) {
        case 16:
            return DP1_REV16;
        case 32:
            return DP1_REV32;
        case 64:
            return DP1_REV64;
        default:
            assert(false);
            return 0;
        }
        break;
    default:
        assert(false);
        return (enum DP1Opcode) BAD_OPCODE;
    }
}

static enum DP2Opcode to_dp2_opcode(int opcode)
{
    switch (opcode) {
    case EBPF_OP_LSH_IMM:
    case EBPF_OP_LSH_REG:
    case EBPF_OP_LSH64_IMM:
    case EBPF_OP_LSH64_REG:
        return DP2_LSLV;
    case EBPF_OP_RSH_IMM:
    case EBPF_OP_RSH_REG:
    case EBPF_OP_RSH64_IMM:
    case EBPF_OP_RSH64_REG:
        return DP2_LSRV;
    case EBPF_OP_ARSH_IMM:
    case EBPF_OP_ARSH_REG:
    case EBPF_OP_ARSH64_IMM:
    case EBPF_OP_ARSH64_REG:
        return DP2_ASRV;
    case EBPF_OP_DIV_IMM:
    case EBPF_OP_DIV_REG:
    case EBPF_OP_DIV64_IMM:
    case EBPF_OP_DIV64_REG:
        return DP2_UDIV;
    default:
        assert(false);
        return (enum DP2Opcode) BAD_OPCODE;
    }
}

static enum LoadStoreOpcode to_loadstore_opcode(int opcode)
{
    switch (opcode) {
    case EBPF_OP_LDXW:
        return LS_LDRW;
    case EBPF_OP_LDXH:
        return LS_LDRH;
    case EBPF_OP_LDXB:
        return LS_LDRB;
    case EBPF_OP_LDXDW:
        return LS_LDRX;
    case EBPF_OP_STW:
    case EBPF_OP_STXW:
        return LS_STRW;
    case EBPF_OP_STH:
    case EBPF_OP_STXH:
        return LS_STRH;
    case EBPF_OP_STB:
    case EBPF_OP_STXB:
        return LS_STRB;
    case EBPF_OP_STDW:
    case EBPF_OP_STXDW:
        return LS_STRX;
    default:
        assert(false);
        return (enum LoadStoreOpcode) BAD_OPCODE;
    }
}

static enum Condition to_condition(int opcode)
{
    uint8_t jmp_type = opcode & EBPF_JMP_OP_MASK;
    switch (jmp_type) {
    case EBPF_MODE_JEQ:
        return COND_EQ;
    case EBPF_MODE_JGT:
        return COND_HI;
    case EBPF_MODE_JGE:
        return COND_HS;
    case EBPF_MODE_JLT:
        return COND_LO;
    case EBPF_MODE_JLE:
        return COND_LS;
    case EBPF_MODE_JSET:
        return COND_NE;
    case EBPF_MODE_JNE:
        return COND_NE;
    case EBPF_MODE_JSGT:
        return COND_GT;
    case EBPF_MODE_JSGE:
        return COND_GE;
    case EBPF_MODE_JSLT:
        return COND_LT;
    case EBPF_MODE_JSLE:
        return COND_LE;
    default:
        assert(false);
        return COND_NV;
    }
}

static inline void emit_cmp(struct jit_state *state, int src, int dst)
{
    emit_addsub_register(state, true, AS_SUBS, RZ, dst, src);
}

static inline void emit_cmp32(struct jit_state *state, int src, int dst)
{
    emit_addsub_register(state, false, AS_SUBS, RZ, dst, src);
}

static inline void emit_cmp_imm32(struct jit_state *state, int dst, int32_t imm)
{
    emit_load_imm(state, R10, imm);
    emit_addsub_register(state, false, AS_SUBS, RZ, dst, R10);
}

static void muldivmod(struct jit_state *state,
                      uint8_t opcode,
                      int src,
                      int dst,
                      int32_t imm)
{
    switch (opcode) {
    case 0x28:
        emit_dataprocessing_threesource(state, false, DP3_MADD, dst, dst, src, RZ);
        break;
    case 0x2f:
        emit_dataprocessing_threesource(state, true, DP3_MADD, dst, dst, src, RZ);
        break;
    case 0x38:
        divmod(state, EBPF_OP_DIV_REG, dst, dst, src);
        break;
    case 0x98:
        divmod(state, EBPF_OP_MOD_REG, dst, dst, src);
        break;
    }
}
static void emit_call(struct jit_state *state, uintptr_t func)
{
    uint32_t stack_movement = align_to(8, 16);
    emit_addsub_immediate(state, true, AS_SUB, SP, SP, stack_movement);
    emit_loadstore_immediate(state, LS_STRX, R30, SP, 0);

    emit_movewide_immediate(state, true, temp_register, func);
    emit_unconditionalbranch_register(state, BR_BLR, temp_register);

    /* On exit need to move result from r0 to whichever register we've mapped
     * EBPF r0 to.  */
    enum Registers dest = map_register(0);
    if (dest != R0) {
        emit_logical_register(state, true, LOG_ORR, dest, RZ, R0);
    }

    emit_loadstore_immediate(state, LS_LDRX, R30, SP, 0);
    emit_addsub_immediate(state, true, AS_ADD, SP, SP, stack_movement);
}

static const char *opcode_table[] = {
#define _(inst, can_branch, insn_len, reg_mask) [rv_insn_##inst] = #inst,
    RV_INSN_LIST
#undef _
};

static void divmod(struct jit_state *state,
                   uint8_t opcode,
                   int rd,
                   int rn,
                   int rm)
{
    bool mod =
        (opcode & EBPF_ALU_OP_MASK) == (EBPF_OP_MOD_IMM & EBPF_ALU_OP_MASK);
    bool sixty_four = (opcode & EBPF_CLS_MASK) == EBPF_CLS_ALU64;
    enum Registers div_dest = mod ? temp_div_register : rd;

    /* Do not need to treet divide by zero as special because the UDIV
     * instruction already returns 0 when dividing by zero.
     */
    emit_dataprocessing_twosource(state, sixty_four, DP2_UDIV, div_dest, rn,
                                  rm);
    if (mod) {
        emit_dataprocessing_threesource(state, sixty_four, DP3_MSUB, rd, rm,
                                        div_dest, rn);
    }
}

static inline void emit_jcc_offset(struct jit_state *state, int code)
{
    switch (code) {
    case 0x84: /* BEQ */
        code = COND_EQ;
        break;
    case 0x85: /* BNE */
        code = COND_NE;
        break;
    case 0x8c: /* BLT */
        code = COND_LT;
        break;
    case 0x8d: /* BGE */
        code = COND_GE;
        break;
    case 0x82: /* BLTU */
        code = COND_LO;
        break;
    case 0x83: /* BGEU */
        code = COND_HS;
        break;
    }
    emit_instruction(state, BR_Bcond | (0 << 5) | code);
}

static inline void emit_store_imm32(struct jit_state *state,
                                    enum operand_size size,
                                    int dst,
                                    int32_t offset,
                                    int32_t imm)
{
    emit_load_imm(state, R10, imm);
    emit_store(state, size, R10, dst, offset);
}

static inline void emit_jump_target_offset(struct jit_state *state,
                                           uint32_t jump_loc,
                                           uint32_t jump_state_offset)
{
    struct jump *jump = &state->jumps[state->num_jumps++];
    jump->offset_loc = jump_loc;
    jump->target_offset = jump_state_offset;
}

static void translate(struct jit_state *state,
                      riscv_t *rv,
                      block_t *block,
                      set_t *set)
{
    uint32_t idx, jump_loc;
    memory_t *m = ((state_t *) rv->userdata)->mem;
    rv_insn_t *ir, *next;
    for (idx = 0, ir = block->ir_head; idx < block->n_insn; idx++, ir = next) {
        // printf("offset = %u\n", offsetof(struct riscv_internal, X));
        // printf("ir->opcode = %u\n", ir->opcode);
        // printf("state->offset = %#lx\n", (uint64_t) state->buf);
        // printf("state->offset = %#x\n", state->offset);
        // printf("insn = %s\n", opcode_table[ir->opcode]);
        // printf("rv->X[%d] = %#x\n", ir->rs1, rv->X[ir->rs1]);
        // printf("rv->X[%d] = %#x\n", ir->rs2, rv->X[ir->rs2]);
        // printf("imm = %d\n", ir->imm);
        switch (ir->opcode) {
        case rv_insn_nop:
            break;
        case rv_insn_lui:
            emit_load_imm(state, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_auipc:
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_jal:
            if (ir->rd) {
                emit_load_imm(state, R6, ir->pc + 4);
                emit_store(state, S32, R6, platform_parameter_registers[0],
                           offsetof(struct riscv_internal, X) + 4 * ir->rd);
            }
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_jmp(state, ir->pc + ir->imm);
            emit_exit(&(*state));
            break;
        case rv_insn_jalr:
            if (ir->rd) {
                emit_load_imm(state, R6, ir->pc + 4);
                emit_store(state, S32, R6, platform_parameter_registers[0],
                           offsetof(struct riscv_internal, X) + 4 * ir->rd);
            }
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 0, R6, ir->imm);
            emit_alu32_imm32(state, 0x81, 4, R6, ~1U);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_beq:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x84);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, R6, ir->pc + 4);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bne:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, R6, ir->pc + 4);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_blt:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x8c);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, R6, ir->pc + 4);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bge:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x8d);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, R6, ir->pc + 4);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bltu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, R6, ir->pc + 4);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bgeu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x83);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, R6, ir->pc + 4);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_lb:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load_sext(state, S8, R6, R7, 0);
            emit_store(state, S32, R7, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lh:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load_sext(state, S16, R6, R7, 0);
            emit_store(state, S32, R7, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lw:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S32, R6, R7, 0);
            emit_store(state, S32, R7, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lbu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S8, R6, R7, 0);
            emit_store(state, S32, R7, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lhu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S16, R6, R7, 0);
            emit_store(state, S32, R7, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sb:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S8, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S8, R7, R6, 0);
            break;
        case rv_insn_sh:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S16, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S16, R7, R6, 0);
            break;
        case rv_insn_sw:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, R7, R6, 0);
            break;
        case rv_insn_addi:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 0, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_slti:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, R6, ir->imm);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc, state->offset);
            break;
        case rv_insn_sltiu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, R6, ir->imm);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc, state->offset);
            break;
        case rv_insn_xori:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 6, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_ori:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 1, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_andi:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 4, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_slli:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 4, R6, ir->imm & 0x1f);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_srli:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 5, R6, ir->imm & 0x1f);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_srai:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 7, R6, ir->imm & 0x1f);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_add:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x01, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sub:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x29, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sll:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R8,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32_imm32(state, 0x81, 4, R8, 0x1f);
            emit_alu32(state, 0xd3, 4, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_slt:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc, state->offset);
            break;
        case rv_insn_sltu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, R7, R6);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc, state->offset);
            break;
        case rv_insn_xor:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x31, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_srl:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R8,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32_imm32(state, 0x81, 4, R8, 0x1f);
            emit_alu32(state, 0xd3, 5, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sra:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R8,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32_imm32(state, 0x81, 4, R8, 0x1f);
            emit_alu32(state, 0xd3, 7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_or:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x09, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_and:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x21, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_ecall:
            emit_load_imm(state, R6, ir->pc);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_call(state, (intptr_t) rv->io.on_ecall);
            emit_exit(&(*state));
            break;
        case rv_insn_ebreak:
            emit_load_imm(state, R6, ir->pc);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_call(state, (intptr_t) rv->io.on_ebreak);
            emit_exit(&(*state));
            break;
#if RV32_HAS(EXT_M)
        case rv_insn_mul:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x28, R7, R6, 0);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_mulh:
            emit_load_sext(state, S32, platform_parameter_registers[0], R6,
                           offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_sext(state, S32, platform_parameter_registers[0], R7,
                           offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x2f, R7, R6, 0);
            emit_alu64_imm8(state, 0xc1, 5, R6, 32);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_mulhsu:
            emit_load_sext(state, S32, platform_parameter_registers[0], R6,
                           offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x2f, R7, R6, 0);
            emit_alu64_imm8(state, 0xc1, 5, R6, 32);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_mulhu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x2f, R7, R6, 0);
            emit_alu64_imm8(state, 0xc1, 5, R6, 32);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_div:
            /* not handle overflow */
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x38, R7, R6, 0);
            emit_cmp_imm32(state, R7, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            emit_load_imm(state, R6, -1);
            emit_jump_target_offset(state, jump_loc, state->offset);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_divu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x38, R7, R6, 0);
            emit_cmp_imm32(state, R7, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            emit_load_imm(state, R6, ~0U);
            emit_jump_target_offset(state, jump_loc, state->offset);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_rem:
            /* not handle overflow */
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x98, R7, R6, 0);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_remu:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x98, R7, R6, 0);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
#endif
        case rv_insn_caddi4spn:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * rv_reg_sp);
            emit_alu32_imm32(state, 0x81, 0, R6, (uint16_t) ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_clw:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7,
                          (intptr_t) (m->mem_base + (uint32_t) ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S32, R6, R7, 0);
            emit_store(state, S32, R7, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_csw:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, R7,
                          (intptr_t) (m->mem_base + (uint32_t) ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, R7, R6, 0);
            break;
        case rv_insn_cnop:
            break;
        case rv_insn_caddi:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rd);
            emit_alu32_imm32(state, 0x81, 0, R6, (int16_t) ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cjal:
            emit_load_imm(state, R6, ir->pc + 2);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * rv_reg_ra);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            // printf("state->offset = %#x\n", state->offset);
            emit_jmp(state, ir->pc + ir->imm);
            emit_exit(&(*state));
            break;
        case rv_insn_cli:
            emit_load_imm(state, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_caddi16sp:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rd);
            emit_alu32_imm32(state, 0x81, 0, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_clui:
            emit_load_imm(state, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_csrli:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 5, R6, ir->shamt);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            break;
        case rv_insn_csrai:
            /* not good */
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 7, R6, ir->shamt);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            break;
        case rv_insn_candi:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 4, R6, ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            break;
        case rv_insn_csub:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x29, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cxor:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x31, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cor:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x09, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cand:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x21, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cj:
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            // printf("state->offset = %#x\n", state->offset);
            emit_jmp(state, ir->pc + ir->imm);
            emit_exit(&(*state));
            break;
        case rv_insn_cbeqz:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, R6, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x84);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 2);
            emit_load_imm(state, R6, ir->pc + 2);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cbnez:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, R6, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 2);
            emit_load_imm(state, R6, ir->pc + 2);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, R6, ir->pc + ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cslli:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rd);
            emit_alu32_imm8(state, 0xc1, 4, R6, (uint8_t) ir->imm);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_clwsp:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * rv_reg_sp);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S32, R6, R7, 0);
            emit_store(state, S32, R7, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cjr:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cmv:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cebreak:
            emit_load_imm(state, R6, ir->pc);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_load_imm(state, R6, 1);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, compressed));
            emit_call(state, (intptr_t) rv->io.on_ebreak);
            emit_exit(&(*state));
            break;
        case rv_insn_cjalr:
            emit_load_imm(state, R6, ir->pc + 2);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * rv_reg_ra);
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cadd:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x01, R7, R6);
            emit_store(state, S32, R6, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cswsp:
            emit_load(state, S32, platform_parameter_registers[0], R6,
                      offsetof(struct riscv_internal, X) + 4 * rv_reg_sp);
            emit_load_imm(state, R7, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, R7, R6);
            emit_load(state, S32, platform_parameter_registers[0], R7,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, R7, R6, 0);
            break;
        default:
            printf("opcode = %s\n", opcode_table[ir->opcode]);
            assert(NULL);
        }
        // emit_exit(&(*state));
        next = ir->next;
    }
}

static void resolve_jumps(struct jit_state *state)
{
    for (unsigned i = 0; i < state->num_jumps; ++i) {
        struct jump jump = state->jumps[i];

        int32_t target_loc;
        if (jump.target_offset != 0) {
            target_loc = jump.target_offset;
        } else if (jump.target_pc == TARGET_PC_EXIT) {
            target_loc = state->exit_loc;
        } else if (jump.target_pc == TARGET_PC_ENTER) {
            target_loc = state->entry_loc;
        } else {
            target_loc = jump.offset_loc + sizeof(uint32_t);
            for (int i = 0; i < state->num_insn; i++) {
                if (jump.target_pc == state->offset_map[i].PC) {
                    target_loc = state->offset_map[i].offset;
                    break;
                }
            }
        }

        int32_t rel = target_loc - jump.offset_loc;
        update_branch_immediate(state, jump.offset_loc, rel);
    }
}

static void translate_chained_block(struct jit_state *state,
                                    riscv_t *rv,
                                    block_t *block,
                                    set_t *set)
{
    if (set_has(set, block->pc_start))
        return;
    set_add(set, block->pc_start);
    offset_map_insert(state, block->pc_start);
    translate(state, rv, block, set);
    rv_insn_t *ir = block->ir_tail;
#if RV32_HAS(JIT)
    if (ir->branch_untaken && !set_has(set, ir->pc + 4)) {
        block_t *block1 = cache_get(rv->block_cache, ir->pc + 4);
        if (block1)
            translate_chained_block(state, rv, block1, set);
    }
    if (ir->branch_taken && !set_has(set, ir->pc + ir->imm)) {
        block_t *block1 = cache_get(rv->block_cache, ir->pc + ir->imm);
        if (block1)
            translate_chained_block(state, rv, block1, set);
    }
#endif
}

uint32_t ubpf_translate_arm64(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    memset(state->offset_map, 0, UBPF_MAX_INSTS * sizeof(struct offset_map));
    memset(state->jumps, 0, UBPF_MAX_INSTS * sizeof(struct jump));
    state->num_insn = 0;
    state->num_jumps = 0;
    uint32_t entry_loc = state->offset;
    set_t set;
    set_reset(&set);
    translate(state, rv, block, &set);
    // translate_chained_block(&(*state), rv, block, &set);
    if (state->offset == state->size) {
        printf("Target buffer too small\n");
        goto out;
    }
    resolve_jumps(&(*state));
out:
    return entry_loc;
}

static void prepare_translate(struct jit_state *state)
{
    uint32_t register_space = _countof(callee_saved_registers) * 8 + 2 * 8;
    state->stack_size = align_to(UBPF_STACK_SIZE + register_space, 16);
    emit_addsub_immediate(state, true, AS_SUB, SP, SP, state->stack_size);

    /* Set up frame */
    emit_loadstorepair_immediate(state, LSP_STPX, R29, R30, SP, 0);
    /* In ARM64 calling convention, R29 is the frame pointer. */
    emit_addsub_immediate(state, true, AS_ADD, R29, SP, 0);

    /* Save callee saved registers */
    for (size_t i = 0; i < _countof(callee_saved_registers); i += 2) {
        emit_loadstorepair_immediate(state, LSP_STPX, callee_saved_registers[i],
                                     callee_saved_registers[i + 1], SP,
                                     (i + 2) * 8);
    }

    emit_unconditionalbranch_register(state, BR_BR, R1);
    /* Epilogue */
    state->exit_loc = state->offset;

    /* Move register 0 into R0 */
    if (map_register(0) != R0) {
        emit_logical_register(state, true, LOG_ORR, R0, RZ, map_register(0));
    }

    /* Restore callee-saved registers).  */
    for (size_t i = 0; i < _countof(callee_saved_registers); i += 2) {
        emit_loadstorepair_immediate(state, LSP_LDPX, callee_saved_registers[i],
                                     callee_saved_registers[i + 1], SP,
                                     (i + 2) * 8);
    }
    emit_loadstorepair_immediate(state, LSP_LDPX, R29, R30, SP, 0);
    emit_addsub_immediate(state, true, AS_ADD, SP, SP, state->stack_size);
    emit_unconditionalbranch_register(state, BR_RET, R30);
}

struct jit_state *init_state(size_t size)
{
    struct jit_state *state = malloc(sizeof(struct jit_state));
    state->offset = 0;
    state->size = size;
    state->buf = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(state->buf != MAP_FAILED);
    prepare_translate(state);
    state->offset_map = calloc(UBPF_MAX_INSTS, sizeof(struct offset_map));
    state->jumps = calloc(UBPF_MAX_INSTS, sizeof(struct jump));
    return state;
}