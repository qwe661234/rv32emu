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
#include "ubpf_jit_x86_64.h"
#include "utils.h"

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

#define UBPF_STACK_SIZE 512
#define UBPF_MAX_INSTS 65536

#if !defined(_countof)
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

static void muldivmod(struct jit_state *state,
                      uint8_t opcode,
                      int src,
                      int dst,
                      int32_t imm);

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

#if defined(_WIN32)
static int platform_nonvolatile_registers[] = {RBP, RBX, RDI, RSI,
                                               R13, R14, R15};
static int platform_parameter_registers[] = {RCX, RDX, R8, R9};
#define RCX_ALT R10
static int register_map[REGISTER_MAP_SIZE] = {
    RAX, R10, RDX, R8, R9, R14, R15, RDI, RSI, RBX, RBP,
};
#else
#define RCX_ALT R9
static int platform_nonvolatile_registers[] = {RBP, RBX, R13, R14, R15};
static int platform_parameter_registers[] = {RDI, RSI, RDX, RCX, R8, R9};
static int register_map[REGISTER_MAP_SIZE] = {
    RAX, RDI, RSI, RDX, R9, R8, RBX, R13, R14, R15, RBP,
};
#endif

/* Return the x86 register for the given eBPF register */
static int map_register(int r)
{
    assert(r < _BPF_REG_MAX);
    return register_map[r % _BPF_REG_MAX];
}

static inline void emit_local_call(struct jit_state *state, uint32_t target_pc)
{
    /*
     * Pushing 4 * 8 = 32 bytes will maintain the invariant
     * that the stack is 16-byte aligned.
     */
    emit_push(state, map_register(BPF_REG_6));
    emit_push(state, map_register(BPF_REG_7));
    emit_push(state, map_register(BPF_REG_8));
    emit_push(state, map_register(BPF_REG_9));
#if defined(_WIN32)
    /* Windows x64 ABI requires home register space
     * Allocate home register space - 4 registers
     */
    emit_alu64_imm32(state, 0x81, 5, RSP, 4 * sizeof(uint64_t));
#endif
    emit1(state, 0xe8);  // e8 is the opcode for a CALL
    emit_jump_target_address(state, target_pc);
#if defined(_WIN32)
    /* Deallocate home register space - 4 registers */
    emit_alu64_imm32(state, 0x81, 0, RSP, 4 * sizeof(uint64_t));
#endif
    emit_pop(state, map_register(BPF_REG_9));
    emit_pop(state, map_register(BPF_REG_8));
    emit_pop(state, map_register(BPF_REG_7));
    emit_pop(state, map_register(BPF_REG_6));
}

static uint32_t emit_retpoline(struct jit_state *state)
{
    /*
     * Using retpolines to mitigate spectre/meltdown. Adapting the approach
     * from
     * https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/technical-documentation/retpoline-branch-target-injection-mitigation.html
     */

    /* label0: */
    /* call label1 */
    uint32_t retpoline_target = state->offset;
    emit1(state, 0xe8);
    uint32_t label1_call_offset = state->offset;
    emit4(state, 0x00);

    /* capture_ret_spec: */
    /* pause */
    uint32_t capture_ret_spec = state->offset;
    emit1(state, 0xf3);
    emit1(state, 0x90);
    /* jmp  capture_ret_spec */
    emit1(state, 0xe9);
    emit_jump_target_offset(state, state->offset, capture_ret_spec);
    emit4(state, 0x00);

    /* label1: */
    /* mov rax, (rsp) */
    uint32_t label1 = state->offset;
    emit1(state, 0x48);
    emit1(state, 0x89);
    emit1(state, 0x04);
    emit1(state, 0x24);

    /* ret */
    emit1(state, 0xc3);

    emit_jump_target_offset(state, label1_call_offset, label1);

    return retpoline_target;
}

/* For testing, this changes the mapping between x86 and eBPF registers */
void ubpf_set_register_offset(int x)
{
    int i;
    if (x < REGISTER_MAP_SIZE) {
        int tmp[REGISTER_MAP_SIZE];
        memcpy(tmp, register_map, sizeof(register_map));
        for (i = 0; i < REGISTER_MAP_SIZE; i++) {
            register_map[i] = tmp[(i + x) % REGISTER_MAP_SIZE];
        }
    } else {
        /* Shuffle array */
        unsigned int seed = x;
        for (i = 0; i < REGISTER_MAP_SIZE - 1; i++) {
            int j = i + (rand_r(&seed) % (REGISTER_MAP_SIZE - i));
            int tmp = register_map[j];
            register_map[j] = register_map[i];
            register_map[i] = tmp;
        }
    }
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

#define UPDATE_PC(pc)                                            \
    emit_load_imm(state, RAX, (pc));                             \
    emit_store(state, S32, RAX, platform_parameter_registers[0], \
               offsetof(struct riscv_internal, PC));

static void prepare_translate(struct jit_state *state)
{
    /* Save platform non-volatile registers */
    for (uint32_t i = 0; i < _countof(platform_nonvolatile_registers); i++)
        emit_push(state, platform_nonvolatile_registers[i]);
    /*
     * Assuming that the stack is 16-byte aligned right before
     * the call insn that brought us to this code, when
     * we start executing the jit'd code, we need to regain a 16-byte
     * alignment. The UBPF_STACK_SIZE is guaranteed to be
     * divisible by 16. However, if we pushed an even number of
     * registers on the stack when we are saving state (see above),
     * then we have to add an additional 8 bytes to get back
     * to a 16-byte alignment.
     */
    if (!(_countof(platform_nonvolatile_registers) % 2))
        emit_alu64_imm32(state, 0x81, 5, RSP, 0x8);

    /* Set BPF R10 (the way to access the frame in eBPF) to match RSP. */

    emit_mov(state, RSP, map_register(BPF_REG_10));

    /* Allocate stack space */
    emit_alu64_imm32(state, 0x81, 5, RSP, UBPF_STACK_SIZE);

#if defined(_WIN32)
    /* Windows x64 ABI requires home register space */
    /* Allocate home register space - 4 registers */
    emit_alu64_imm32(state, 0x81, 5, RSP, 4 * sizeof(uint64_t));
#endif

    /* Jump to the entry point, the entry point is stored in the second
     * parameter. */
    emit1(state, 0xff);
    emit1(state, 0xe6);

    /* Epilogue */
    state->exit_loc = state->offset;
    /* Move register 0 into rax */
    if (map_register(BPF_REG_0) != RAX)
        emit_mov(state, map_register(BPF_REG_0), RAX);

    /* Deallocate stack space by restoring RSP from BPF R10. */
    emit_mov(state, map_register(BPF_REG_10), RSP);

    if (!(_countof(platform_nonvolatile_registers) % 2))
        emit_alu64_imm32(state, 0x81, 0, RSP, 0x8);

    /* Restore platform non-volatile registers */
    for (uint32_t i = 0; i < _countof(platform_nonvolatile_registers); i++) {
        emit_pop(state, platform_nonvolatile_registers
                            [_countof(platform_nonvolatile_registers) - i - 1]);
    }
    /* Return */
    emit1(state, 0xc3);
}

static const char *opcode_table[] = {
#define _(inst, can_branch, insn_len, reg_mask) [rv_insn_##inst] = #inst,
    RV_INSN_LIST
#undef _
};

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
        switch (ir->opcode) {
        case rv_insn_nop:
            break;
        case rv_insn_lui:
            emit_load_imm(state, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_auipc:
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_jal:
            if (ir->rd) {
                emit_load_imm(state, RAX, ir->pc + 4);
                emit_store(state, S32, RAX, platform_parameter_registers[0],
                           offsetof(struct riscv_internal, X) + 4 * ir->rd);
            }
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_jmp(state, ir->pc + ir->imm);
            emit_exit(&(*state));
            break;
        case rv_insn_jalr:
            if (ir->rd) {
                emit_load_imm(state, RAX, ir->pc + 4);
                emit_store(state, S32, RAX, platform_parameter_registers[0],
                           offsetof(struct riscv_internal, X) + 4 * ir->rd);
            }
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 0, RAX, ir->imm);
            emit_alu32_imm32(state, 0x81, 4, RAX, ~1U);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_beq:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x84);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, RAX, ir->pc + 4);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bne:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, RAX, ir->pc + 4);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_blt:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x8c);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, RAX, ir->pc + 4);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bge:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x8d);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, RAX, ir->pc + 4);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bltu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, RAX, ir->pc + 4);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_bgeu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x83);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 4);
            emit_load_imm(state, RAX, ir->pc + 4);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_lb:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load_sext(state, S8, RAX, RBX, 0);
            emit_store(state, S32, RBX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lh:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load_sext(state, S16, RAX, RBX, 0);
            emit_store(state, S32, RBX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lw:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S32, RAX, RBX, 0);
            emit_store(state, S32, RBX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lbu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S8, RAX, RBX, 0);
            emit_store(state, S32, RBX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_lhu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S16, RAX, RBX, 0);
            emit_store(state, S32, RBX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sb:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S8, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S8, RBX, RAX, 0);
            break;
        case rv_insn_sh:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S16, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S16, RBX, RAX, 0);
            break;
        case rv_insn_sw:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, RBX, RAX, 0);
            break;
        case rv_insn_addi:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 0, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_slti:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, RAX, ir->imm);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            break;
        case rv_insn_sltiu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, RAX, ir->imm);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            break;
        case rv_insn_xori:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 6, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_ori:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 1, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_andi:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 4, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_slli:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 4, RAX, ir->imm & 0x1f);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_srli:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 5, RAX, ir->imm & 0x1f);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_srai:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 7, RAX, ir->imm & 0x1f);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_add:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x01, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sub:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x29, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sll:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RCX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32_imm32(state, 0x81, 4, RCX, 0x1f);
            emit_alu32(state, 0xd3, 4, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_slt:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            break;
        case rv_insn_sltu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_cmp32(state, RBX, RAX);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             1);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x82);
            emit_store_imm32(state, S32, platform_parameter_registers[0],
                             offsetof(struct riscv_internal, X) + 4 * ir->rd,
                             0);
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            break;
        case rv_insn_xor:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x31, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_srl:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RCX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32_imm32(state, 0x81, 4, RCX, 0x1f);
            emit_alu32(state, 0xd3, 5, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_sra:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RCX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32_imm32(state, 0x81, 4, RCX, 0x1f);
            emit_alu32(state, 0xd3, 7, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_or:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x09, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_and:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x21, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_ecall:
            emit_load_imm(state, RAX, ir->pc);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_call(state, (intptr_t) rv->io.on_ecall);
            emit_exit(&(*state));
            break;
        case rv_insn_ebreak:
            emit_load_imm(state, RAX, ir->pc);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_call(state, (intptr_t) rv->io.on_ebreak);
            emit_exit(&(*state));
            break;
#if RV32_HAS(EXT_M)
        case rv_insn_mul:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x28, RBX, RAX, 0);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_mulh:
            emit_load_sext(state, S32, platform_parameter_registers[0], RAX,
                           offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_sext(state, S32, platform_parameter_registers[0], RBX,
                           offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x2f, RBX, RAX, 0);
            emit_alu64_imm8(state, 0xc1, 5, RAX, 32);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_mulhsu:
            emit_load_sext(state, S32, platform_parameter_registers[0], RAX,
                           offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x2f, RBX, RAX, 0);
            emit_alu64_imm8(state, 0xc1, 5, RAX, 32);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_mulhu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x2f, RBX, RAX, 0);
            emit_alu64_imm8(state, 0xc1, 5, RAX, 32);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_div:
            /* not handle overflow */
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x38, RBX, RAX, 0);
            emit_cmp_imm32(state, RBX, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            emit_load_imm(state, RAX, -1);
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_divu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x38, RBX, RAX, 0);
            emit_cmp_imm32(state, RBX, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            emit_load_imm(state, RAX, ~0U);
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_rem:
            /* not handle overflow */
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x98, RBX, RAX, 0);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_remu:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            muldivmod(state, 0x98, RBX, RAX, 0);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
#endif
        case rv_insn_caddi4spn:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * rv_reg_sp);
            emit_alu32_imm32(state, 0x81, 0, RAX, (uint16_t) ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_clw:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX,
                          (intptr_t) (m->mem_base + (uint32_t) ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S32, RAX, RBX, 0);
            emit_store(state, S32, RBX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_csw:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load_imm(state, RBX,
                          (intptr_t) (m->mem_base + (uint32_t) ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, RBX, RAX, 0);
            break;
        case rv_insn_cnop:
            break;
        case rv_insn_caddi:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rd);
            emit_alu32_imm32(state, 0x81, 0, RAX, (int16_t) ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cjal:
            emit_load_imm(state, RAX, ir->pc + 2);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * rv_reg_ra);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            // printf("state->offset = %#x\n", state->offset);
            emit_jmp(state, ir->pc + ir->imm);
            emit_exit(&(*state));
            break;
        case rv_insn_cli:
            emit_load_imm(state, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_caddi16sp:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rd);
            emit_alu32_imm32(state, 0x81, 0, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_clui:
            emit_load_imm(state, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_csrli:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 5, RAX, ir->shamt);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            break;
        case rv_insn_csrai:
            /* not good */
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm8(state, 0xc1, 7, RAX, ir->shamt);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            break;
        case rv_insn_candi:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_alu32_imm32(state, 0x81, 4, RAX, ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            break;
        case rv_insn_csub:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x29, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cxor:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x31, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cor:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x09, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cand:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x21, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cj:
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            // printf("state->offset = %#x\n", state->offset);
            emit_jmp(state, ir->pc + ir->imm);
            emit_exit(&(*state));
            break;
        case rv_insn_cbeqz:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, RAX, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x84);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 2);
            emit_load_imm(state, RAX, ir->pc + 2);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cbnez:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_cmp_imm32(state, RAX, 0);
            jump_loc = state->offset;
            emit_jcc_offset(state, 0x85);
            if (ir->branch_untaken)
                emit_jmp(state, ir->pc + 2);
            emit_load_imm(state, RAX, ir->pc + 2);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            emit_jump_target_offset(state, jump_loc + 2, state->offset);
            if (ir->branch_taken)
                emit_jmp(state, ir->pc + ir->imm);
            emit_load_imm(state, RAX, ir->pc + ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cslli:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rd);
            emit_alu32_imm8(state, 0xc1, 4, RAX, (uint8_t) ir->imm);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_clwsp:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * rv_reg_sp);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S32, RAX, RBX, 0);
            emit_store(state, S32, RBX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cjr:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cmv:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cebreak:
            emit_load_imm(state, RAX, ir->pc);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_load_imm(state, RAX, 1);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, compressed));
            emit_call(state, (intptr_t) rv->io.on_ebreak);
            emit_exit(&(*state));
            break;
        case rv_insn_cjalr:
            emit_load_imm(state, RAX, ir->pc + 2);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * rv_reg_ra);
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, PC));
            emit_exit(&(*state));
            break;
        case rv_insn_cadd:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs1);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_alu32(state, 0x01, RBX, RAX);
            emit_store(state, S32, RAX, platform_parameter_registers[0],
                       offsetof(struct riscv_internal, X) + 4 * ir->rd);
            break;
        case rv_insn_cswsp:
            emit_load(state, S32, platform_parameter_registers[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * rv_reg_sp);
            emit_load_imm(state, RBX, (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, RBX, RAX);
            emit_load(state, S32, platform_parameter_registers[0], RBX,
                      offsetof(struct riscv_internal, X) + 4 * ir->rs2);
            emit_store(state, S32, RBX, RAX, 0);
            break;
        default:
            printf("opcode = %d\n", ir->opcode);
            assert(NULL);
        }
        next = ir->next;
    }
}

static void muldivmod(struct jit_state *state,
                      uint8_t opcode,
                      int src,
                      int dst,
                      int32_t imm)
{
    bool mul =
        (opcode & EBPF_ALU_OP_MASK) == (EBPF_OP_MUL_IMM & EBPF_ALU_OP_MASK);
    bool div =
        (opcode & EBPF_ALU_OP_MASK) == (EBPF_OP_DIV_IMM & EBPF_ALU_OP_MASK);
    bool mod =
        (opcode & EBPF_ALU_OP_MASK) == (EBPF_OP_MOD_IMM & EBPF_ALU_OP_MASK);
    bool is64 = (opcode & EBPF_CLS_MASK) == EBPF_CLS_ALU64;
    bool reg = (opcode & EBPF_SRC_REG) == EBPF_SRC_REG;

    // Short circuit for imm == 0.
    if (!reg && imm == 0) {
        assert(NULL);
        if (div || mul) {
            // For division and multiplication, set result to zero.
            emit_alu32(state, 0x31, dst, dst);
        } else {
            // For modulo, set result to dividend.
            emit_mov(state, dst, dst);
        }
        return;
    }

    if (dst != RAX) {
        emit_push(state, RAX);
    }

    if (dst != RDX) {
        emit_push(state, RDX);
    }

    // Load the divisor into RCX.
    if (imm) {
        emit_load_imm(state, RCX, imm);
    } else {
        emit_mov(state, src, RCX);
    }

    // Load the dividend into RAX.
    emit_mov(state, dst, RAX);

    // BPF has two different semantics for division and modulus. For division
    // if the divisor is zero, the result is zero.  For modulus, if the divisor
    // is zero, the result is the dividend. To handle this we set the divisor
    // to 1 if it is zero and then set the result to zero if the divisor was
    // zero (for division) or set the result to the dividend if the divisor was
    // zero (for modulo).

    if (div || mod) {
        // Check if divisor is zero.
        if (is64) {
            emit_alu64(state, 0x85, RCX, RCX);
        } else {
            emit_alu32(state, 0x85, RCX, RCX);
        }

        // Save the dividend for the modulo case.
        if (mod) {
            emit_push(state, RAX);  // Save dividend.
        }

        // Save the result of the test.
        emit1(state, 0x9c); /* pushfq */

        // Set the divisor to 1 if it is zero.
        emit_load_imm(state, RDX, 1);
        emit1(state, 0x48);
        emit1(state, 0x0f);
        emit1(state, 0x44);
        emit1(state, 0xca); /* cmove rcx,rdx */

        /* xor %edx,%edx */
        emit_alu32(state, 0x31, RDX, RDX);
    }

    if (is64) {
        emit_rex(state, 1, 0, 0, 0);
    }

    // Multiply or divide.
    emit_alu32(state, 0xf7, mul ? 4 : 6, RCX);

    // Division operation stores the remainder in RDX and the quotient in RAX.
    if (div || mod) {
        // Restore the result of the test.
        emit1(state, 0x9d); /* popfq */

        // If zero flag is set, then the divisor was zero.

        if (div) {
            // Set the dividend to zero if the divisor was zero.
            emit_load_imm(state, RCX, 0);

            // Store 0 in RAX if the divisor was zero.
            // Use conditional move to avoid a branch.
            emit1(state, 0x48);
            emit1(state, 0x0f);
            emit1(state, 0x44);
            emit1(state, 0xc1); /* cmove rax,rcx */
        } else {
            // Restore dividend to RCX.
            emit_pop(state, RCX);

            // Store the dividend in RAX if the divisor was zero.
            // Use conditional move to avoid a branch.
            emit1(state, 0x48);
            emit1(state, 0x0f);
            emit1(state, 0x44);
            emit1(state, 0xd1); /* cmove rdx,rcx */
        }
    }

    if (dst != RDX) {
        if (mod) {
            emit_mov(state, RDX, dst);
        }
        emit_pop(state, RDX);
    }
    if (dst != RAX) {
        if (div || mul) {
            emit_mov(state, RAX, dst);
        }
        emit_pop(state, RAX);
    }
}

static void resolve_jumps(struct jit_state *state)
{
    int i;
    for (i = 0; i < state->num_jumps; i++) {
        struct jump jump = state->jumps[i];

        int target_loc;
        if (jump.target_offset != 0) {
            target_loc = jump.target_offset;
        } else if (jump.target_pc == TARGET_PC_EXIT) {
            target_loc = state->exit_loc;
        } else if (jump.target_pc == TARGET_PC_RETPOLINE) {
            target_loc = state->retpoline_loc;
        } else {
            target_loc = jump.offset_loc + sizeof(uint32_t);
            for (int i = 0; i < state->num_insn; i++) {
                if (jump.target_pc == state->offset_map[i].PC) {
                    target_loc = state->offset_map[i].offset;
                    break;
                }
            }
        }
        /* Assumes jump offset is at end of instruction */
        uint32_t rel = target_loc - (jump.offset_loc + sizeof(uint32_t));

        uint8_t *offset_ptr = &state->buf[jump.offset_loc];
        memcpy(offset_ptr, &rel, sizeof(uint32_t));
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

uint32_t ubpf_translate_x86_64(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    memset(state->offset_map, 0, UBPF_MAX_INSTS * sizeof(struct offset_map));
    memset(state->jumps, 0, UBPF_MAX_INSTS * sizeof(struct jump));
    state->num_insn = 0;
    state->num_jumps = 0;
    uint32_t entry_loc = state->offset;
    set_t set;
    set_reset(&set);
    translate_chained_block(&(*state), rv, block, &set);

    if (state->offset == state->size) {
        printf("Target buffer too small\n");
        goto out;
    }
    resolve_jumps(&(*state));
out:
    return entry_loc;
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