set -xe

# The nets and pins of every iteration of a Verilog generate loop have the
# name of the iteration

which nvc

nvc --std=19 -a $TESTDIR/netlist/m5g_genloop.sv -e m5g_genloop
nvc --std=19 --netlist m5g_genloop > loop.json

grep -q '^"g\[0\].r":{' loop.json
grep -q '^"g\[1\].r":{' loop.json

# The cells of the second iteration use the pins of the second iteration
grep -q '^"g\[1\]/r":' loop.json
grep -q "^\"g\[1\]/r'\":" loop.json
[ $(grep -c '^"g\[0\]/r":' loop.json) = 6 ]
[ $(grep -c '^"g\[1\]/r":' loop.json) = 6 ]
