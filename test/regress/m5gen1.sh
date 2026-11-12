set -xe

# Generate blocks and the instances in them in the symbols of a scan have
# the names (and so the paths) of the instances in the elaborated design

which nvc

nvc --scan --symbols $TESTDIR/scan/m5g_generate.sv > scan.json

nvc --std=19 -a $TESTDIR/scan/m5g_generate.sv -e top
nvc --std=19 --netlist top > top.json

# Named blocks of the four kinds of construct
grep -q '"name":"g","kind":"generate","container":"WORK.TOP","gen":"for"' scan.json
grep -q '"name":"h","kind":"generate","container":"WORK.TOP.g","gen":"for"' scan.json
grep -q '"name":"yes","kind":"generate","container":"WORK.TOP","gen":"if"' scan.json
grep -q '"name":"no","kind":"generate","container":"WORK.TOP","gen":"if"' scan.json
grep -q '"name":"two","kind":"generate","container":"WORK.TOP","gen":"case"' scan.json
grep -q '"name":"other","kind":"generate","container":"WORK.TOP","gen":"case"' scan.json

# Instances in blocks, including nested ones
grep -q '"name":"u","kind":"instance","detail":"leaf","container":"WORK.TOP.g"' scan.json
grep -q '"name":"v","kind":"instance","detail":"leaf","container":"WORK.TOP.g.h"' scan.json
grep -q '"name":"c1","kind":"instance","detail":"leaf","container":"WORK.TOP.yes"' scan.json
grep -q '"name":"c4","kind":"instance","detail":"leaf","container":"WORK.TOP.two"' scan.json

# The same in the elaborated design
grep -q '^"top.g\[1\].h\[0\].v":{' top.json
grep -q '^"top.yes.c1":{' top.json
grep -q '^"top.two.c4":{' top.json

# Unnamed blocks are numbered as elaboration does
grep -q '"name":"genblk4","kind":"generate","container":"WORK.TOP","gen":"if"' scan.json
grep -q '"name":"c6","kind":"instance","detail":"leaf","container":"WORK.TOP.genblk4"' scan.json
grep -q '^"top.genblk4.c6":{' top.json
grep -q '"name":"genblk5","kind":"generate","container":"WORK.TOP","gen":"if"' scan.json
grep -q '^"top.genblk5.c7":{' top.json
grep -q '"name":"genblk6","kind":"generate","container":"WORK.TOP","gen":"for"' scan.json
grep -q '"name":"c8","kind":"instance","detail":"leaf","container":"WORK.TOP.genblk6"' scan.json
grep -q '^"top.genblk6\[0\].c8":{' top.json

# Branches not taken are in the scan only
grep -q '"name":"c2","kind":"instance"' scan.json
[ $(grep -c '^"top.no.c2":{' top.json) = 0 ]
