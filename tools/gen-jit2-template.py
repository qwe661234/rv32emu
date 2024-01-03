#!/usr/bin/env python3

'''
This script serves as a code generator for creating JIT code templates 
based on existing code files in the 'src' directory, eliminating the need 
for writing duplicated code.
'''

import re
import sys

INSN = {
    "Zifencei": ["fencei"],
    "Zicsr": [
        "csrrw",
        "csrrs",
        "csrrc",
        "csrrw",
        "csrrsi",
        "csrrci"],
    "EXT_M": [
        "mul",
        "mulh",
        "mulhsu",
        "mulhu",
        "div",
        "divu",
        "rem",
        "remu"],
    "EXT_A": [
        "lrw",
        "scw",
        "amoswapw",
        "amoaddw",
        "amoxorw",
        "amoandw",
        "amoorw",
        "amominw",
        "amomaxw",
        "amominuw",
        "amomaxuw"],
    "EXT_F": [
        "flw",
        "fsw",
        "fmadds",
        "fmsubs",
        "fnmsubs",
        "fnmadds",
        "fadds",
        "fsubs",
        "fmuls",
        "fdivs",
        "fsqrts",
        "fsgnjs",
        "fsgnjns",
        "fsgnjxs",
        "fmins",
        "fmaxs",
        "fcvtws",
        "fcvtwus",
        "fmvxw",
        "feqs",
        "flts",
        "fles",
        "fclasss",
        "fcvtsw",
        "fcvtswu",
        "fmvwx"],
    "EXT_C": [
        "caddi4spn",
        "clw",
        "csw",
        "cnop",
        "caddi",
        "cjal",
        "cli",
        "caddi16sp",
        "clui",
        "csrli",
        "csrai",
        "candi",
        "csub",
        "cxor",
        "cor",
        "cand",
        "cj",
        "cbeqz",
        "cbnez",
        "cslli",
        "clwsp",
        "cjr",
        "cmv",
        "cebreak",
        "cjalr",
        "cadd",
        "cswsp",
    ],
}
EXT_LIST = ["Zifencei", "Zicsr", "EXT_M", "EXT_A", "EXT_F", "EXT_C"]
SKIPLIST = [
    "jal",
    "jalr",
    "beq",
    "bne",
    "blt",
    "bge",
    "bltu",
    "bgeu",
    "lb",
    "lh",
    "lw",
    "lbu",
    "lhu",
    "sb",
    "sh",
    "sw",
    "slli",
    "srli",
    "srai",
    "flw",
    "fsw",
    "clw",
    "csw",
    "cjal",
    "cj",
    "cjalr",
    "cjr",
    "cbeqz",
    "cbnez",
    "clwsp",
    "cswsp"
]
# check enabled extension in Makefile


def parse_argv(EXT_LIST, SKIPLIST):
    for argv in sys.argv:
        if argv.find("RV32_FEATURE_") != -1:
            ext = argv[argv.find("RV32_FEATURE_") + 13:-2]
            if argv[-1:] == "1" and EXT_LIST.count(ext):
                EXT_LIST.remove(ext)
    for ext in EXT_LIST:
        SKIPLIST += INSN[ext]


def remove_comment(str):
    str = re.sub(r'//[\s|\S]+?\n', "", str)
    return re.sub(r'/\*[\s|\S]+?\*/', "", str)


parse_argv(EXT_LIST, SKIPLIST)
# prepare PROLOGUE
output = '''#define PROLOGUE \\
\"#include <stdint.h>\\n\"\\
\"#include <stdbool.h>\\n\"\\
'''
f = open('src/io.h', 'r')
lines = f.read()
lines = remove_comment(lines)
output = output + "\"" + \
    re.sub("\n", "\"\\\n\"", re.findall(
        r'typedef[\S|\s]+?memory_t;', lines)[0]) + "\"\\\n"
f = open('src/riscv.h', 'r')
lines = f.read()
lines = remove_comment(lines)
reg_info = re.findall(r'#define RV_REGS_LIST[\S|\s]+?};', lines)[0]
reg_info = re.sub(r"/\*[\S|\s]+?\*/", "", reg_info)
reg_list = re.findall(r"_\([\S|\s]+?\)", reg_info)
reg_info = "enum {"
for i in range(32):
    reg_info = reg_info + "rv_reg_" + reg_list[i][2:-1] + ","
reg_info += "N_RV_REGS };"
output = output + "\"" + \
    re.sub("\n", "\"\\\n\"", reg_info) + "\"\\\n"
output = output + "\"" + \
    re.sub("\n", "\"\\\n\"", re.findall(
        r'typedef[\S|\s]+?riscv_exception_t;', lines)[0]) + "\"\\\n"
output = output + "\"" + "typedef struct { uint32_t v; } float32_t;\\\n" + "\"\\\n"
output = output + "\"" + "typedef float32_t riscv_float_t;\\\n" + "\"\\\n"
output = output + "\"" + \
    re.sub("\n", "\"\\\n\"", re.findall(
        r'typedef riscv_word_t[\S|\s]+?riscv_io_t;', lines)[0]) + "\"\\\n"
state = re.sub("map_t fd_map;", "", re.findall(
        r'typedef struct {\s+?m[\S|\s]+?state_t;', lines)[0])
output = output + "\"" + \
    re.sub("\n", "\"\\\n\"", state) + "\"\\\n"
if sys.argv.count("RV32_FEATURE_EXT_F=1"):
    f = open('src/softfloat.h', 'r')
    lines = f.read()
    lines = remove_comment(lines)
    output = output + "\"" + \
        re.sub("\n", "\"\\\n\"", re.findall(
            r'enum[\S|\s]+?};', lines)[0]) + "\"\\\n"
f = open('src/riscv_private.h', 'r')
lines = f.read()
lines = remove_comment(lines)
lines = re.sub(r'#if RV32_HAS\(GDBSTUB\)[\s|\S]+?#endif\n', "", lines)
lines = re.sub(r'#if !RV32_HAS\(JIT\)[\s|\S]+?#endif\n', "", lines)
lines = re.sub(r"#if RV32_HAS\(EXT_F\)", "", lines)
lines = re.sub('#endif\n', "", lines)
output = output + "\"" + \
    re.sub("\n", "\"\\\n\"", re.findall(
        r'struct riscv_internal[\S|\s]+?bool compressed;', lines)[0] + "};") + "\"\\\n"
output += '''\"static inline uint32_t sign_extend_h(const uint32_t x)\"\\
\"{\"\\
\"    return (int32_t) ((int16_t) x);\"\\
\"}\"\\
\"static inline uint32_t sign_extend_b(const uint32_t x)\"\\
\"{\"\\
\"    return (int32_t) ((int8_t) x);\"\\
\"}\"\\
'''
output += "\"bool start(riscv_t *rv, uint64_t cycle, uint32_t PC) {\"\\\n"
output += "\" uint32_t pc, addr, udividend, udivisor, tmp, data, mask, ures, \"\\\n"
output += "\"a, b, jump_to;\"\\\n"
output += "\"  int32_t dividend, divisor, res;\"\\\n"
output += "\"  int64_t multiplicand, multiplier;\"\\\n"
output += "\"  uint64_t umultiplier;\"\\\n"
output += "\"  memory_t *m = ((state_t *)rv->userdata)->mem;\"\n"

f = open('src/rv32_template.c', 'r')
lines = f.read()
lines = remove_comment(lines)
# remove exception handler
lines = re.sub(r'RV_EXC[\S]+?\([\S|\s]+?\);\s', "", lines)
# replace variable
lines = re.sub(r'const [u]*int[32|64]+_t', "", lines)
lines = re.sub("uint32_t tmp", "tmp", lines)
lines = re.sub("uint32_t a", "a", lines)
lines = re.sub("uint32_t b", "b", lines)
lines = re.sub("uint32_t a_sign", "a_sign", lines)
lines = re.sub("uint32_t b_sign", "b_sign", lines)
lines = re.sub("uint32_t data", "data", lines)
lines = re.sub("uint32_t bits", "bits", lines)
str2 = re.findall(r'RVOP\([\s|\S]+?},', lines)
op = []
impl = []
for i in range(len(str2)):
    tmp = remove_comment(str2[i])
    op.append(tmp[5:tmp.find(',')].strip())
    impl.append(tmp[tmp.find('{') + 1:-2])

f.close()
# generate jit template
for i in range(len(str2)):
    if (not SKIPLIST.count(op[i])):
        output = output + "RVOP(" + op[i] + ", {\n"
        substr = impl[i].split("\n")
        for str in substr:
            IRs = re.findall(
                r'ir->[rd|rs1|rs2|rs3|imm|imm2|insn_len|shamt]+', str)
            str = re.sub(
                r'ir->[rd|rs1|rs2|rs3|imm|imm2|insn_len|shamt]+', "%u", str)
            if (str != ""):
                if (len(IRs)):
                    output = output + 'GEN(\"' + str + '\\n\"'
                    for IR in IRs:
                        output = output + ", " + IR
                else:
                    output = output + 'strcat(gencode, \"' + str + '\\n\"'
                output = output + ');\n'
        output = output + "})\n"
sys.stdout.write(output)
