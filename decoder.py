#!/usr/bin/env python3

import argparse
from graphviz import Digraph

parser = argparse.ArgumentParser(prog='decoder.py')
parser.add_argument('filename', type=str, help='filename')
args = parser.parse_args()

profile_dict = {}
f = open(f'./build/{args.filename}.prof', 'r')
fileds = f.readline()
raw_datas = f.read().split("\n")
for data in raw_datas:
    tmp = data.split("|")
    if len(tmp) == 9:
        d = {
            "PC_start": tmp[0].strip(),
            "PC_end": tmp[1].strip(),
            "frequency": tmp[2].strip(),
            "hot": tmp[3].strip(),
            "backward": tmp[4].strip(),
            "loop": tmp[5].strip(),
            "untaken": tmp[6].strip(),
            "taken": tmp[7].strip(),
            "IR_list": tmp[8].strip(),
        }
        profile_dict[int(d["PC_start"], 16)] = d;

dot = Digraph(comment='Profiling Graph')
dot.graph_attr["ratio"] = "compress"
for key in [*profile_dict.keys()]:
    tiered = int(profile_dict[key]["hot"], 10)
    color = ""
    if (tiered == 0):
        color = "white"
    elif (tiered == 1):
        color = "blue"
    elif (tiered == 2):
        color = "red"
    dot.node(profile_dict[key]["PC_start"], profile_dict[key]["PC_start"], fillcolor=color, style='filled')
for key in [*profile_dict.keys()]:
    if (profile_dict[key]["taken"] != "NULL"):
        dot.edge(profile_dict[key]["PC_start"], profile_dict[key]["taken"], "")
    if (profile_dict[key]["untaken"] != "NULL"):
        dot.edge(profile_dict[key]["PC_start"], profile_dict[key]["untaken"], "")
dot.render(f'./{args.filename}.gv', view=False)
