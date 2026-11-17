set -xe

# VHDL array of arrays: a small one is bit by bit, a large one a memory

which nvc

nvc --std=08 -a $TESTDIR/netlist/mem2.vhd -e mem2
nvc --std=08 --netlist mem2 --output=m.json

grep -q '^"memories":{' m.json
grep -q '^"RAM":{' m.json
grep -q '^"width":8' m.json
grep -q '^"size":65536' m.json
grep -q '^"start_offset":0' m.json
grep -q '^"memories_read":"RAM"' m.json
grep -q '^"memories_written":"RAM"' m.json
! grep -q '^"RAM":\[' m.json
grep -q '^"SMALL":{' m.json
grep -q '^"SMALL":\[[0-9,]*\]' m.json
