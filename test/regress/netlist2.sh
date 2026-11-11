set -xe

# Block schematic of a SystemVerilog design

which nvc

nvc -a $TESTDIR/netlist/sv1.sv -e sv1

nvc --netlist sv1 > out.json

grep -q '^"sv1":{' out.json
grep -q '^"sv1.i0":{' out.json
grep -q '^"sv1.gg\[1\].ig":{' out.json
grep -q '^"language":"verilog"' out.json
grep -q '^"label":"always_ff @15"' out.json
grep -q '^"kind":"always_ff"' out.json
grep -q '^"kind":"assign"' out.json

# Module ports with the direction from the declaration
grep -q '^"io":{' out.json
grep -q '^"direction":"inout"' out.json

# Constant and part select of a vector
grep -q '^"a":\["0","1","0","1"\]' out.json
grep -q '^"w\[3:0\]":' out.json


nvc --netlist --top-only sv1 > top.json
[ $(grep -c '^"sv1.i0":{' top.json) = 0 ]
grep -q '^"type":"sv1.i0"' top.json
