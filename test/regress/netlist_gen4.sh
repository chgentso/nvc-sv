set -xe

# Parameters of a Verilog module instantiated from a VHDL component

nvc --std=19 -a $TESTDIR/netlist/ng_vm.sv $TESTDIR/netlist/ng_vh.vhd -e ng_vh
nvc --std=19 --netlist ng_vh --output=out.json
awk -f $TESTDIR/regress/ng_extract.awk out.json > got.txt

cat >want.txt <<'EOT'
"NG_VH.U" "W" "integer" "integer" 6 true
"NG_VH.U" "K" "[7:0]" "bits" "8'h12" true
"NG_VH.U" "N" "integer" "integer" 7 true
EOT

diff -u want.txt got.txt

# Backward compatible: the old parameters are still written
grep -q '^"parameters":{' out.json
