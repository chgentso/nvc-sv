set -xe

# Parameters of Verilog modules in instances, instance arrays and generate loops, with overrides by name and by position

nvc --std=19 -a $TESTDIR/netlist/ng_vm.sv -e ng_vtop
nvc --std=19 --netlist ng_vtop --output=out.json
awk -f $TESTDIR/regress/ng_extract.awk out.json > got.txt

cat >want.txt <<'EOT'
"ng_vtop.ux" "S" "string" "string" "a\"b" true
"ng_vtop.ux" "R" "real" "real" 2.5 true
"ng_vtop.ux" "T" "type" "int" true
"ng_vtop.ux" "SG" "[7:0]" "bits" "8'shfe" false
"ng_vtop.ux_def" "S" "string" "string" "dflt" false
"ng_vtop.ux_def" "R" "real" "real" 1.5 false
"ng_vtop.ux_def" "T" "type" "logic [3:0]" false
"ng_vtop.ux_def" "SG" "[7:0]" "bits" "8'shfe" false
"ng_vtop.u0" "W" "integer" "integer" 3 true
"ng_vtop.u0" "K" "[7:0]" "bits" "8'hff" true
"ng_vtop.u0" "N" "integer" "integer" -3 false
"ng_vtop.u1" "W" "integer" "integer" 5 true
"ng_vtop.u1" "K" "[7:0]" "bits" "8'h01" true
"ng_vtop.u1" "N" "integer" "integer" -3 false
"ng_vtop.g[0].u" "W" "integer" "integer" 7 true
"ng_vtop.g[0].u" "K" "[7:0]" "bits" "8'h12" false
"ng_vtop.g[0].u" "N" "integer" "integer" 0 true
"ng_vtop.g[1].u" "W" "integer" "integer" 7 true
"ng_vtop.g[1].u" "K" "[7:0]" "bits" "8'h12" false
"ng_vtop.g[1].u" "N" "integer" "integer" 2 true
EOT

diff -u want.txt got.txt

# Backward compatible: the old parameters are still written
grep -q '^"parameters":{' out.json
