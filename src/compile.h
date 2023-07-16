/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once
#include <stdint.h>

#include "riscv.h"
#include "riscv_private.h"

uint8_t *block_compile(riscv_t *rv, block_vector_t* block_vec);