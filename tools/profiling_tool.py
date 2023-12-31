#!/usr/bin/env python3

import re
import sys

chain = []
f = open('profile/' + sys.argv[1], 'r')
lines = f.read()
lines = lines[lines.find("PC ="):]
data = lines.split("PC =")
for dd in data:
    if dd != "":
        dd = dd.split("\n")
        a = []
        if len(dd) > 4:
            a.append(dd[0][1:]);
            a.append(dd[1][dd[1].find("backward = ") + 11:]);
            a.append(dd[2][dd[2].find("invoke time = ") + 14:]);
            a.append(dd[3][dd[3].find("ir_cnt = ") + 9:]);
            a.append(dd[4][dd[4].find("loop = ") + 7:]);
            a.append(dd[5][dd[5].find("entry = ") + 8:]);
            a.append(dd[6][dd[6].find("frequency = ") + 12:]);
            chain.append(a);
        else:
            a.append(dd[0][1:]);
            a.append(dd[1][dd[1].find("entry = ") + 8:]);
            a.append(dd[2][dd[2].find("frequency = ") + 12:]);
            chain.append(a);

noc = []
f = open('profile_noc/' + sys.argv[1], 'r')
lines = f.read()
lines = lines[lines.find("PC ="):]
data = lines.split("PC =")
for dd in data:
    if dd != "":
        dd = dd.split("\n")
        a = []
        a.append(dd[0][1:]);
        a.append(dd[1][dd[1].find("frequency = ") + 12:]);
        noc.append(a);

chain = sorted(chain)
noc = sorted(noc)
i = 0
j = 0
hotspot = []
print("PC     | noc Frequency | PC     | Frequency | entry_point")
while(j < len(chain) and i < len(noc)):
    if noc[i][0] != chain[j][0]:
        if noc[i][0] > chain[j][0]:
            if (len(chain[j]) <= 3):
                print("{:7}| {:14}| {:7}| {:10}| {:8}".format("", "", chain[j][0], chain[j][2], chain[j][1]))
            else:
                print("{:7}| {:14}| {:7}| {:10}| {:8}".format("", "", chain[j][0], chain[j][6], chain[j][5]))
            j += 1;
        else:
            print("{:7}| {:14}| {:7}| {:10}".format(noc[i][0], noc[i][1], "", ""))
            i += 1
    else:
        if (len(chain[j]) <= 3):
            print("{:7}| {:14}| {:7}| {:10}| {:8}".format(noc[i][0], noc[i][1], chain[j][0], chain[j][2], chain[j][1]))
            i += 1
            j += 1;
        else:
            print("{:7}| {:14}| {:7}| {:10}| {:8}".format(noc[i][0], noc[i][1], chain[j][0], chain[j][6], chain[j][5]))
            a = [];
            a.append(noc[i][0]);
            a.append(noc[i][1]);
            a.append(chain[j][0]);
            a.append(chain[j][6]);
            a.append(chain[j][1]);
            a.append(chain[j][2]);
            a.append(chain[j][3]);
            a.append(chain[j][4]);
            a.append(chain[j][5]);
            hotspot.append(a);
            i += 1
            j += 1;
while (i < len(noc)):
    print("{:7}| {:14}| {:7}| {:10}".format(noc[i][0], noc[i][1], "", ""))
    i += 1
while (j < len(chain)):
    if (len(chain[j]) <= 3):
        print("{:7}| {:14}| {:7}| {:10}".format("", "", chain[j][0], chain[j][2], chain[j][1]))
    else:
        print("{:7}| {:14}| {:7}| {:10}".format("", "", chain[j][0], chain[j][6], chain[j][5]))
    j += 1;
print("PC     | noc Frequency | PC     | Frequency | backward | invoke time | ir_cnt | loop | entry")
for hot in hotspot:
    print("{:7}| {:14}| {:7}| {:10}| {:9}| {:12}| {:6} | {:6} | {:6}".format(hot[0], hot[1], hot[2], hot[3], hot[4], hot[5], hot[6], hot[7], hot[8]))

# backward = 0;
# loop = 0;
# hotcnt = 0;
# IRCNT = [0, 0, 0, 0];
# print("PC     | invoke time | backward | ir_cnt | loop")
# for hot in hotspot:
#     if int(hot[5]) >= 4096:
#         hotcnt += 1
#         if hot[4] == "1":
#             backward += 1;
#         if hot[7] == "1":
#             loop += 1;
#         if int(hot[6]) <= 50:
#             IRCNT[0] += 1
#         elif int(hot[6]) > 50 and int(hot[6]) <= 100:
#             IRCNT[1] += 1
#         elif int(hot[6]) > 100 and int(hot[6]) <= 200:
#             IRCNT[2] += 1
#         elif int(hot[6]) > 200:
#             IRCNT[3] += 1
#         print("{:7}| {:12}| {:8} | {:6} | {:6}".format(hot[2], hot[5], hot[4], hot[6], hot[7]))

# print("hotspot && invoke time >= 4096 = {}".format(hotcnt))
# print("backward = {}".format(backward))
# print("loop = {}".format(loop))
# print("ir_cnt <= 50 = {}".format(IRCNT[0]))
# print("50 < ir_cnt <= 100 = {}".format(IRCNT[1]))
# print("100 < ir_cnt <= 200 = {}".format(IRCNT[2]))
# print("ir_cnt > 200 = {}".format(IRCNT[3]))
