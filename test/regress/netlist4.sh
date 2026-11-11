set -xe

# Block schematic with SystemVerilog interfaces and VHDL records

which nvc

nvc -a $TESTDIR/netlist/iface1.sv -e iface1
nvc --netlist iface1 > iface.json

# An interface instance whose signals are all nets of the enclosing module
# is not drawn as a cell of its own
[ $(grep -c '^"iface1.bi0":{' iface.json) = 0 ]
[ $(grep -c '^"bi0":{' iface.json) = 0 ]

# The signals are nets of the enclosing module named for the instance
grep -q '^"bi0.data":{' iface.json
grep -q '^"bi1.valid":{' iface.json
grep -q '^"interface":1' iface.json

# A module with an interface port has a pin for each signal in the modport
grep -q '^"b/data":"output"' iface.json
grep -q '^"b/data":"input"' iface.json
grep -q '^"b/clk":"input"' iface.json

# Fields of a packed structure are nets of their own
grep -q '^"hdr.kind":{' iface.json
grep -q '^"hdr.last":{' iface.json

nvc --std=08 -a $TESTDIR/netlist/vhdl2.vhd -e vhdl2
nvc --std=08 --netlist vhdl2 > rec.json

grep -q '^"R":{' rec.json
grep -q '^"R.A":{' rec.json
grep -q '^"R.B":{' rec.json
grep -q '^"ARR(0).A":{' rec.json
grep -q '^"ARR(1).B":{' rec.json
grep -q '^"record":1' rec.json
grep -q '^"field":1' rec.json
