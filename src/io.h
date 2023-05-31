/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

void memory_new();
void memory_delete();

/* read a C-style string from memory */
uint32_t memory_read_str(uint8_t *dst, uint32_t addr, uint32_t max);

/* read an instruction from memory */
uint32_t memory_ifetch(uint32_t addr);

/* read a word from memory */
uint32_t memory_read_w(uint32_t addr);

/* read a short from memory */
uint16_t memory_read_s(uint32_t addr);

/* read a byte from memory */
uint8_t memory_read_b(uint32_t addr);

/* read a length of data from memory */
void memory_read(uint8_t *dst, uint32_t addr, uint32_t size);

void memory_write(uint32_t addr, const uint8_t *src, uint32_t size);

void memory_write_w(uint32_t addr, const uint8_t *src);

void memory_write_s(uint32_t addr, const uint8_t *src);

void memory_write_b(uint32_t addr, const uint8_t *src);

void memory_fill(uint32_t addr, uint32_t size, uint8_t val);
