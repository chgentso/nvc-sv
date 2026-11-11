set -xe

# Block schematic of a VHDL design: generate, slices, constants,
# expressions and inout ports

which nvc

nvc --std=08 -a $TESTDIR/netlist/vhdl1.vhd -e vhdl1

nvc --std=08 --netlist vhdl1 --output=out.json

# One module per elaborated instance with generate blocks inlined
grep -q '^"VHDL1":{' out.json
grep -q '^"VHDL1.G(0).U_LEAF":{' out.json
grep -q '^"VHDL1.G(1).U_LEAF":{' out.json
grep -q '^"VHDL1.U_CONST":{' out.json
grep -q '^"VHDL1.U_EXPR":{' out.json
[ $(grep -c '^"top":1' out.json) = 1 ]
[ $(grep -c '^"VHDL1.G(0)":{' out.json) = 0 ]

# Cells and their pin directions
grep -q '^"type":"VHDL1.G(1).U_LEAF"' out.json
grep -q '^"IO":"inout"' out.json
grep -q '^"cell":"\$process"' out.json
grep -q '^"type":"P_COUNT"' out.json
grep -q "^\"CLK'\":\"output\"" out.json
grep -q '^"label":"P_COUNT"' out.json
grep -q '^"cell":"\$expr"' out.json
grep -q '^"type":"D and \\"0011\\""' out.json
grep -q '^"expr":"D and \\"0011\\""' out.json

# Constant actual, least significant bit first
grep -q '^"A":\["0","1","0","1"\]' out.json

# Slice of a vector: the process reads D(1 downto 0) and drives Q(3 downto 2)
grep -q '^"Q\[3:2\]":"output"' out.json
grep -q '^"Q\[3:2\]":\[[0-9]*,[0-9]*\]' out.json
grep -q '^"D\[1:0\]":"input"' out.json

# Output goes to stdout by default
nvc --std=08 --netlist vhdl1 > stdout.json
cmp out.json stdout.json

# Only the top-level module
nvc --std=08 --netlist --top-only vhdl1 > top.json
grep -q '^"VHDL1":{' top.json
[ $(grep -c '^"VHDL1.G(0).U_LEAF":{' top.json) = 0 ]
grep -q '^"type":"VHDL1.G(0).U_LEAF"' top.json

nvc --std=08 --netlist --depth=1 vhdl1 > depth1.json
grep -q '^"VHDL1.G(0).U_LEAF":{' depth1.json

# Not elaborated
if nvc --std=08 --netlist leaf; then
  echo "missing error for design which is not elaborated"
  exit 1
fi

if nvc --std=08 --netlist --depth=-1 vhdl1; then
  echo "missing error for negative depth"
  exit 1
fi
