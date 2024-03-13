#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "decode.h"

typedef intptr_t (*funcPtr_t)(riscv_t *);

funcPtr_t t2(riscv_t *rv, uint64_t mem_base);