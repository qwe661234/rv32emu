#!/usr/bin/env python3

output = ""

emus = ["t1", "rvt1", "rvt2", "rvtiered", "tiered", "long_rvtiered"]
tests = ["dhrystone", "qsort", "miniz", "primes", "sha","numeric_sort", "FP_emulation",
         "bitfield", "stream", "string_sort", "assignment", "idea", "huffman"]
for emu in emus:
    output = output + "Metric" + "," +  emu + "," + "L1-icache-misses" + "," + "iTLB-misses" + "," + "page-faults" + "\n"
    for test in tests:
        f = open(f'./perf/{test}.prof.{emu}', 'r')
        lines = f.read().split("\n")
        cache_miss = lines[5][0:lines[5].find(
            "L1-icache-misses")].replace(",", "").strip()
        iTLB_misses = lines[6][0:lines[6].find(
            "iTLB-misses")].replace(",", "").strip()
        page_faults = lines[7][0:lines[7].find("page-faults")].replace(",", "").strip()
        performance = lines[9][0:lines[9].find("+-")].strip()
        output = output + test + "," + performance + "," + cache_miss + "," + iTLB_misses + \
            "," + page_faults + "\n"
        f.close()
print(output)
