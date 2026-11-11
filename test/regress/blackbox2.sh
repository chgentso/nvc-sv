set -xe

# --black-box-missing: Verilog modules that cannot be found, and VHDL and
# Verilog together

which nvc

if nvc -a $TESTDIR/blackbox/vlog1.sv -e vlog1 2> plain.txt; then exit 1; fi
grep -q "module 'cellbuf' not found" plain.txt

nvc -a $TESTDIR/blackbox/vlog1.sv -e --black-box-missing vlog1 2> boxed.txt
grep -q 'black box for missing unit CELLBUF (library WORK)' boxed.txt
grep -q 'black box for missing unit CELLPLL' boxed.txt
grep -q 'black box for missing unit CELLMUX' boxed.txt
test "$(grep -c 'black box for missing unit CELLBUF' boxed.txt)" = 1

if nvc -r vlog1 2> run.txt; then exit 1; fi
grep -q 'contains black boxes for missing units' run.txt

nvc --netlist vlog1 --output=vlog1.json
# The cells are of the type of the missing module
grep -q '^"type":"cellbuf"' vlog1.json
grep -q '^"type":"cellpll"' vlog1.json
grep -q '^"type":"cellmux"' vlog1.json
grep -q '^"blackbox":1' vlog1.json
grep -q '^"inferred_ports":1' vlog1.json
# Named connections: names and widths from the actuals
grep -q '^"cellbuf":{' vlog1.json
test "$(grep -c '^"cellbuf":{' vlog1.json)" = 1
grep -q '^"direction":"inout"' vlog1.json
# Positional connections: p0, p1, ...
grep -q '^"p0":' vlog1.json
grep -q '^"p2":' vlog1.json
# Part-selected and unconnected actuals
grep -q '^"oe":' vlog1.json
grep -q '^"y":' vlog1.json

# A list of libraries has to name the work library to include Verilog
if nvc -a $TESTDIR/blackbox/vlog1.sv -e --black-box-missing=vendor vlog1 \
       2> list.txt; then
    exit 1
fi
grep -q "module 'cellbuf' not found" list.txt

# VHDL with a component and an entity that are missing, instantiating Verilog
# that instantiates a module that is missing
nvc --std=08 -a --black-box-missing $TESTDIR/blackbox/vsub.v \
    $TESTDIR/blackbox/mixed1.vhd -e --black-box-missing mixed1 2> mixed.txt
grep -q 'black box for missing unit CELLBUF (library WORK)' mixed.txt
grep -q 'black box for missing unit CELLPLL (library PRIMS)' mixed.txt
grep -q 'black box for missing unit CELLINV' mixed.txt

if nvc --std=08 -r mixed1 2> mixed_run.txt; then exit 1; fi
grep -q 'contains black boxes for missing units' mixed_run.txt

nvc --std=08 --netlist mixed1 --output=mixed1.json
grep -q '^"type":"CELLBUF"' mixed1.json
grep -q '^"type":"CELLPLL"' mixed1.json
grep -q '^"type":"cellinv"' mixed1.json
grep -q '^"language":"verilog"' mixed1.json
grep -q '^"language":"vhdl"' mixed1.json
test "$(grep -c '^"blackbox":1' mixed1.json)" -ge 3
