set -xe

# Large unpacked arrays are written as memories, not bit lists

which nvc

nvc --std=08 -a $TESTDIR/netlist/mem1.sv -e mem1
nvc --std=08 --netlist --evidence mem1 --output=m.json

if which python3; then
  python3 -I -c 'import json; json.load(open("m.json"))'
fi

# The memories section of the top module
grep -q '^"memories":{' m.json
grep -q '^"mem":{' m.json
grep -q '^"width":8' m.json
grep -q '^"start_offset":0' m.json
grep -q '^"size":1048576' m.json
grep -q '^"dims":"\[3:0\]\[0:511\]\[0:255\]"' m.json
grep -q '^"size":524288' m.json
grep -q '^"memories_read":"mem tab"' m.json
grep -q '^"memories_written":"mem"' m.json
grep -q '^"memory_ports":"ram=mem"' m.json
grep -q '^"memories_written":"ram_local"' m.json

# No netnames and no bit lists for them
! grep -q '^"mem":\[' m.json
! grep -q '^"ram_local":\[' m.json
[ $(wc -c < m.json) -lt 100000 ]

# A small array is still written bit by bit
grep -q '^"tiny":\[[0-9,]*\]' m.json
grep -q '^"tiny":{' m.json
