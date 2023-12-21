// Copyright (c) 2015 Big Switch Networks, Inc
// SPDX-License-Identifier: Apache-2.0

/*
 * Copyright 2015 Big Switch Networks, Inc
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

/*
 * Generic x86-64 code generation functions
 */

#ifndef UBPF_JIT_X86_64_H
#define UBPF_JIT_X86_64_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "riscv_private.h"

// All A64 registers (note SP & RZ get encoded the same way).
enum Registers {
    R0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    R16,
    R17,
    R18,
    R19,
    R20,
    R21,
    R22,
    R23,
    R24,
    R25,
    R26,
    R27,
    R28,
    R29,
    R30,
    SP,
    RZ = 31
};

enum operand_size {
    S8,
    S16,
    S32,
    S64,
};

struct jump {
    uint32_t offset_loc;
    uint32_t target_pc;
    uint32_t target_offset;
};

/* Special values for target_pc in struct jump */
#define TARGET_PC_EXIT ~UINT32_C(0)
#define TARGET_PC_ENTER (~UINT32_C(0) & 0x0101)

// This is guaranteed to be an illegal A64 instruction.
#define BAD_OPCODE ~UINT32_C(0)

struct offset_map {
    uint32_t PC;
    uint32_t offset;
};

struct jit_state {
    uint8_t *buf;
    uint32_t offset;
    uint32_t size;
    uint32_t exit_loc;
    uint32_t entry_loc;
    uint32_t stack_size;
    uint32_t retpoline_loc;
    struct offset_map *offset_map;
    int num_insn;
    struct jump *jumps;
    int num_jumps;
};

struct jit_state *init_state(size_t size);

uint32_t ubpf_translate_arm64(riscv_t *rv, block_t *block);

static inline void offset_map_insert(struct jit_state *state, int32_t target_pc)
{
    struct offset_map *map_entry = &state->offset_map[state->num_insn++];
    map_entry->PC = target_pc;
    map_entry->offset = state->offset;
}
#endif
