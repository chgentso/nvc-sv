set -xe

# --black-box-missing: VHDL component and direct entity instances of units
# that cannot be found are elaborated as empty black boxes

which nvc

# A component that nothing is bound to: elaborates with a warning as always
nvc --std=08 -a $TESTDIR/blackbox/comp2.vhd -e comp2 2> plain.txt
grep -q 'cannot find entity for component CELLBUF' plain.txt
nvc --std=08 -r comp2

# With the option it is a black box: ports from the component declaration
nvc --std=08 -a $TESTDIR/blackbox/comp2.vhd \
    -e --black-box-missing comp2 2> boxed.txt
grep -q 'black box for missing unit CELLBUF (library WORK)' boxed.txt
test "$(grep -c 'black box for missing unit' boxed.txt)" = 1   # Reported once
if grep -q 'cannot find entity' boxed.txt; then exit 1; fi

# ... and cannot be run
if nvc --std=08 -r comp2 2> run.txt; then exit 1; fi
grep -q 'contains black boxes for missing units' run.txt

# Also in the same command line
if nvc --std=08 -e --black-box-missing comp2 -r 2> run2.txt; then exit 1; fi
grep -q 'contains black boxes for missing units' run2.txt

nvc --std=08 --netlist comp2 --output=comp2.json
grep -q '^"type":"CELLBUF"' comp2.json
grep -q '^"blackbox":1' comp2.json
grep -q '^"CELLBUF":{' comp2.json            # The module of the black box
test "$(grep -c '^"CELLBUF":{' comp2.json)" = 1
grep -q '^"I":{' comp2.json
grep -q '^"direction":"input"' comp2.json
grep -q '^"direction":"output"' comp2.json
# The ports of a component are exact, not inferred
if grep -q 'inferred_ports' comp2.json; then exit 1; fi
# The component that has an entity is not a black box
grep -q '^"type":"COMP2.U3.LEAF"' comp2.json

# Entities that are not in any library cannot be analysed without it
if nvc --std=08 -a $TESTDIR/blackbox/direct1.vhd 2> direct_plain.txt; then
    exit 1
fi
grep -q 'no visible declaration for PRIMS.CELLPLL' direct_plain.txt

# With it the ports come from the port maps
nvc --std=08 -a --black-box-missing $TESTDIR/blackbox/direct1.vhd \
    -e --black-box-missing direct1 2> direct.txt
grep -q 'black box for missing unit CELLPLL (library PRIMS)' direct.txt
grep -q 'black box for missing unit CELLBUF (library PRIMS)' direct.txt

# Elaborating without it is an error again, naming the reason
if nvc --std=08 -e direct1 2> direct_e.txt; then exit 1; fi
grep -q 'cannot find entity CELLPLL' direct_e.txt

nvc --std=08 --netlist direct1 --output=direct1.json
grep -q '^"type":"CELLPLL"' direct1.json
grep -q '^"type":"CELLBUF"' direct1.json
grep -q '^"inferred_ports":1' direct1.json
grep -q '^"CLK_IN":' direct1.json
grep -q '^"LOCKED":' direct1.json
grep -q '^"CLK_OUT":' direct1.json          # open: still a port
grep -q '^"EN":' direct1.json
grep -q '^"P2":' direct1.json               # positional: P0, P1, ...

# Only the libraries in the list may be missing
nvc --std=08 -a --black-box-missing=prims $TESTDIR/blackbox/direct1.vhd \
    -e --black-box-missing=prims direct1 2> list.txt
grep -q 'library PRIMS' list.txt
if nvc --std=08 -a --black-box-missing=vendor \
       $TESTDIR/blackbox/direct1.vhd 2> list2.txt; then
    exit 1
fi
grep -q 'no visible declaration for PRIMS.CELLPLL' list2.txt

# A component of the work library is not in the list
nvc --std=08 -a $TESTDIR/blackbox/comp2.vhd \
    -e --black-box-missing=prims comp2 2> list3.txt
grep -q 'cannot find entity for component CELLBUF' list3.txt
nvc --std=08 -r comp2
