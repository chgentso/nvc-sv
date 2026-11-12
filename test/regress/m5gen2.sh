set -xe

# Values of the parameters of Verilog instances in the netlist: on the
# cell and on the module, in declaration order, not for local parameters

which nvc

nvc --std=19 -a $TESTDIR/netlist/m5g_params.sv -e top
nvc --std=19 --netlist top > top.json

# The top has none
grep -q '^"top":{' top.json
[ $(grep -A12 '^"top":{' top.json | grep -c '"parameters"') = 0 ]

# Defaults, vectors, signed, x and z, strings, reals and types
grep -q '^"INIT":"4'"'"'ha"' top.json
grep -q '^"SV":"8'"'"'sh80"' top.json
grep -q '^"XZ":"4'"'"'b10xz"' top.json
grep -q '^"S":"\\"hi\\""' top.json
grep -q '^"R":"0.5"' top.json
grep -q '^"T":"logic \[1:0\]"' top.json

# Positional overrides
grep -q '^"W":"3"' top.json
grep -q '^"S":"\\"abc\\""' top.json
grep -q '^"R":"1e-09"' top.json
grep -q '^"T":"int"' top.json

# defparam, two levels and generate instances
grep -q '^"W":"5"' top.json
grep -q '^"top.mu.u":{' top.json
grep -q '^"top.g\[2\].u":{' top.json

# A local parameter is not listed
[ $(grep -c '^"L":' top.json) = 0 ]

# A Verilog module instantiated from VHDL
nvc --std=08 -a $TESTDIR/netlist/m5g_mixed.v $TESTDIR/netlist/m5g_mixed.vhd -e m5g_mixed
nvc --std=08 --netlist m5g_mixed > mixed.json
grep -q '^"W":"6"' mixed.json
grep -q '^"K":"1"' mixed.json
