set -xe

# Instance arrays, generate blocks and a chain of Verilog, VHDL and Verilog
# instances across the language boundary: one module for each instance

which nvc

nvc --std=08 -a $TESTDIR/netlist/vhdl1.vhd $TESTDIR/netlist/mixed1.v \
    $TESTDIR/netlist/mixed1.vhd $TESTDIR/netlist/mixed3.v -e mixed3

nvc --std=08 --netlist mixed3 > chain.json

grep -q '^"mixed3":{' chain.json
grep -q '^"mixed3.ua\[0\]":{' chain.json
grep -q '^"mixed3.ua\[1\]":{' chain.json
grep -q '^"mixed3.g\[0\].u":{' chain.json
grep -q '^"mixed3.g\[1\].u":{' chain.json
grep -q '^"mixed3.u_mid":{' chain.json
grep -q '^"mixed3.u_mid.U_SUB":{' chain.json

# No module for the wrapper around the entity or around the module
[ $(grep -c '\.LEAF":{' chain.json) = 0 ]
[ $(grep -c '\.vsub":{' chain.json) = 0 ]
[ $(grep -c 'leaf\$' chain.json) = 0 ]

# The name of the unit is the type of the module
grep -q '^"unit":"LEAF"' chain.json
grep -q '^"unit":"MIXED1"' chain.json
grep -q '^"unit":"vsub"' chain.json

# Each instance is a cell named by its label in the module above
grep -q '^"ua\[0\]":{' chain.json
grep -q '^"g\[1\].u":{' chain.json
grep -q '^"U_SUB":{' chain.json
