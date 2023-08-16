/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once
#include "riscv_private.h"

typedef struct {
    int size;
    block_t *arr[1024];
} block_vector_t;

block_vector_t *detect_loop(riscv_t *rv, block_t *root);