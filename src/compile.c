#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cache.h"
#include "codegen.h"
#include "compile.h"
#include "elfdef.h"

#define BINBUF_CAP 64 * 1024

static uint8_t elfbuf[BINBUF_CAP] = {0};

uint8_t *compile(riscv_t *rv, char *source)
{
    int saved_stdout = dup(STDOUT_FILENO);
    int outp[2];

    if (pipe(outp) != 0)
        printf("cannot make a pipe\n");
    dup2(outp[1], STDOUT_FILENO);
    close(outp[1]);

    FILE *f;
    f = popen("clang -O2 -c -xc -o /dev/stdout -", "w");
    if (f == NULL)
        printf("cannot compile program\n");
    fwrite(source, 1, strlen(source), f);
    pclose(f);
    fflush(stdout);

    (void) read(outp[0], elfbuf, BINBUF_CAP);
    dup2(saved_stdout, STDOUT_FILENO);

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *) elfbuf;

    /**
     * for some instructions, clang will generate a corresponding .rodata
     * section. this means we need to write a mini-linker that puts the .rodata
     * section into memory, takes its actual address, and uses symbols and
     * relocations to apply it back to the corresponding location in the .text
     * section.
     */

    long long text_idx = 0, symtab_idx = 0, rela_idx = 0, rodata_idx = 0;
    {
        uint64_t shstr_shoff =
            ehdr->e_shoff + ehdr->e_shstrndx * sizeof(elf64_shdr_t);
        elf64_shdr_t *shstr_shdr = (elf64_shdr_t *) (elfbuf + shstr_shoff);
        assert(ehdr->e_shnum != 0);

        for (long long idx = 0; idx < ehdr->e_shnum; idx++) {
            uint64_t shoff = ehdr->e_shoff + idx * sizeof(elf64_shdr_t);
            elf64_shdr_t *shdr = (elf64_shdr_t *) (elfbuf + shoff);
            char *str =
                (char *) (elfbuf + shstr_shdr->sh_offset + shdr->sh_name);
            if (strcmp(str, ".text") == 0)
                text_idx = idx;
            if (strcmp(str, ".rela.text") == 0)
                rela_idx = idx;
            if (strncmp(str, ".rodata.", strlen(".rodata.")) == 0)
                rodata_idx = idx;
            if (strcmp(str, ".symtab") == 0)
                symtab_idx = idx;
        }
    }

    assert(text_idx != 0 && symtab_idx != 0);

    uint64_t text_shoff = ehdr->e_shoff + text_idx * sizeof(elf64_shdr_t);
    elf64_shdr_t *text_shdr = (elf64_shdr_t *) (elfbuf + text_shoff);
    if (rela_idx == 0 || rodata_idx == 0)
        return code_cache_add(rv->cache, rv->PC, elfbuf + text_shdr->sh_offset,
                              text_shdr->sh_size, text_shdr->sh_addralign);

    uint64_t shoff = ehdr->e_shoff + rodata_idx * sizeof(elf64_shdr_t);
    elf64_shdr_t *shdr = (elf64_shdr_t *) (elfbuf + shoff);
    code_cache_add(rv->cache, rv->PC, elfbuf + shdr->sh_offset, shdr->sh_size,
                   shdr->sh_addralign);
    uint64_t text_addr = (uint64_t) code_cache_add(
        rv->cache, rv->PC, elfbuf + text_shdr->sh_offset, text_shdr->sh_size,
        text_shdr->sh_addralign);

    // apply relocations to .text section.
    {
        uint64_t shoff = ehdr->e_shoff + rela_idx * sizeof(elf64_shdr_t);
        elf64_shdr_t *shdr = (elf64_shdr_t *) (elfbuf + shoff);
        long long rels = shdr->sh_size / sizeof(elf64_rela_t);

        uint64_t symtab_shoff =
            ehdr->e_shoff + symtab_idx * sizeof(elf64_shdr_t);
        elf64_shdr_t *symtab_shdr = (elf64_shdr_t *) (elfbuf + symtab_shoff);

        for (long long idx = 0; idx < rels; idx++) {
#ifndef __x86_64__
            fatal("only support x86_64 for now");
#endif
            elf64_rela_t *rel = (elf64_rela_t *) (elfbuf + shdr->sh_offset +
                                                  idx * sizeof(elf64_rela_t));
            assert(rel->r_type == R_X86_64_PC32);

            elf64_sym_t *sym =
                (elf64_sym_t *) (elfbuf + symtab_shdr->sh_offset +
                                 rel->r_sym * sizeof(elf64_sym_t));
            uint32_t *loc = (uint32_t *) (text_addr + rel->r_offset);
            *loc = (uint32_t) ((long long) sym->st_value + rel->r_addend -
                               (long long) rel->r_offset);
        }
    }

    return (uint8_t *) text_addr;
}
char *gencode = NULL;
uint8_t *block_compile(riscv_t *rv)
{
    if (!gencode)
        gencode = malloc(64 * 1024);
    memset(gencode, 0, 64 * 1024);
    trace_and_gencode(rv, gencode);
    return compile(rv, gencode);
}