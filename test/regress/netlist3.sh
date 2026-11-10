set -xe

# Block schematic across the VHDL and Verilog boundary

which nvc

nvc --std=08 -a $TESTDIR/netlist/mixed1.v $TESTDIR/netlist/mixed1.vhd \
    -e mixed1

nvc --std=08 --netlist mixed1 --output=vhdl_top.json

# The component and the Verilog module bound to it are separate modules
grep -q '^"MIXED1":{' vhdl_top.json
grep -q '^"MIXED1.U_SUB":{' vhdl_top.json
grep -q '^"MIXED1.U_SUB.vsub":{' vhdl_top.json
grep -q '^"language":"verilog"' vhdl_top.json
grep -q '^"language":"vhdl"' vhdl_top.json

# Conversion functions are transparent but recorded on the instance
grep -q '^"conversions":"TO_VERILOG,TO_VHDL"' vhdl_top.json
grep -q '^"kind":"assign"' vhdl_top.json
grep -q '^"kind":"always"' vhdl_top.json

# The output pin of the Verilog module is the same net as Q of the shim
grep -q '^"io":"inout"' vhdl_top.json

# A VHDL entity instantiated from Verilog
nvc --std=08 -a $TESTDIR/netlist/vhdl1.vhd $TESTDIR/netlist/mixed2.v \
    -e mixed2

nvc --std=08 --netlist mixed2 > verilog_top.json

grep -q '^"mixed2":{' verilog_top.json
grep -q '^"mixed2.u_vhdl":{' verilog_top.json
grep -q '^"mixed2.u_vhdl.LEAF":{' verilog_top.json
grep -q '^"label":"P_OUT"' verilog_top.json
