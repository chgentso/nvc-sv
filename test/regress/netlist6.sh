set -xe

# Clocking evidence of VHDL processes and attributes: --netlist --evidence

which nvc

nvc --std=08 -a $TESTDIR/netlist/ev_cdc1.vhd -e ev_cdc1

# Without the option nothing is added
nvc --std=08 --netlist ev_cdc1 > plain.json
[ $(grep -c 'evidence' plain.json) = 0 ]
[ $(grep -c 'async_reg' plain.json) = 0 ]

nvc --std=08 --netlist --evidence ev_cdc1 --output=ev.json

grep -q '^"evidence_version":1' ev.json

# Every process has a class
n=$(grep -c '"cell":"\$process"' ev.json)
[ $n -gt 0 ]
[ $(grep -c '^"evidence":{' ev.json) = $n ]

# The synchroniser: clock, asynchronous reset, copy chain
grep -q '"pin":"CLK_I","edge":"rise"' ev.json
grep -q '"pin":"RST_N_A_I","level":"low","role":"reset"' ev.json
grep -q '^"SYNC1":{' ev.json
grep -q '^"copy":"SYNC0"' ev.json
grep -q '^"copy":"D_I"' ev.json

# Two clock domains
grep -q '"pin":"CLK_A_I","edge":"rise"' ev.json
grep -q '"pin":"CLK_B_I","edge":"rise"' ev.json
grep -q '"pin":"CLK_IN_I","edge":"rise"' ev.json
grep -q '"pin":"CLK_OUT_I","edge":"rise"' ev.json

# Attributes of nets and units
grep -q '^"async_reg":"true"' ev.json
grep -q '^"shreg_extract":"no"' ev.json
grep -q '^"keep_hierarchy":"true"' ev.json

# The cases of one process each
nvc --std=08 -a $TESTDIR/netlist/ev_vhdl1.vhd -e ev_vhdl1
nvc --std=08 --netlist --evidence ev_vhdl1 > cases.json

grep -q '"pin":"CLK","edge":"fall"' cases.json
grep -q '"pin":"CLK2","edge":"rise"' cases.json
grep -q '"pin":"RST_N","level":"low","role":"reset"' cases.json
grep -q '"pin":"RST","level":"high","role":"reset"' cases.json
grep -q '"pin":"SET_N","level":"low","role":"set"' cases.json
grep -q '^"sync_reset":\["RST"\]' cases.json
grep -q '^"class":"mixed"' cases.json
grep -q '^"class":"other"' cases.json
grep -q '^"confidence":"approx"' cases.json
grep -q '^"cdc_depth":"2"' cases.json
grep -q '^"cdc_domain":"clk"' cases.json
grep -q '^"dont_touch":"yes"' cases.json
grep -q '^"dont_touch":"no"' cases.json

# Static indexes into arrays of records, reasons and element bits of clocks
nvc --std=08 -a $TESTDIR/netlist/ev_vhdl2.vhd -e ev_vhdl2
nvc --std=08 --netlist --evidence ev_vhdl2 > static.json

grep -q '"pin":"CHANS(1)/CLK_IN","edge":"rise","bits":\[[0-9]*\]' static.json
grep -q '^"reasons":\["dynamic-index"\]' static.json
grep -q '^"reasons":\["loop"\]' static.json
grep -q '^"reasons":\["call"\]' static.json
[ $(grep -c '^"confidence":"approx"' static.json) = 3 ]

# The pins of the default output are not changed by the option
nvc --std=08 --netlist ev_vhdl2 > static_plain.json
[ $(grep -c 'reasons' static_plain.json) = 0 ]
