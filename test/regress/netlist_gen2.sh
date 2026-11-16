set -xe

# Generics of a VHDL entity instantiated from SystemVerilog with parameters computed from local parameters, package constants, a system function and a macro

nvc --std=19 -a $TESTDIR/netlist/ng_sv_leaf.vhd $TESTDIR/netlist/ng_sv_top.sv -e ng_sv_top
nvc --std=19 --netlist ng_sv_top --output=out.json
awk -f $TESTDIR/regress/ng_extract.awk out.json > got.txt

cat >want.txt <<'EOT'
"ng_sv_top.u_lp" "W" "INTEGER" "integer" 7 true
"ng_sv_top.u_lp" "NAME" "STRING" "string" "sv" true
"ng_sv_top.u_lp" "V" "STD_LOGIC_VECTOR" "bits" "0110" true
"ng_sv_top.u_lp" "B" "BOOLEAN" "boolean" false false
"ng_sv_top.u_lp" "R" "REAL" "real" 0.5 false
"ng_sv_top.u_pkg" "W" "INTEGER" "integer" 12 true
"ng_sv_top.u_pkg" "NAME" "STRING" "string" "def" false
"ng_sv_top.u_pkg" "V" "STD_LOGIC_VECTOR" "bits" "1010" false
"ng_sv_top.u_pkg" "B" "BOOLEAN" "boolean" false false
"ng_sv_top.u_pkg" "R" "REAL" "real" 2.5 true
"ng_sv_top.u_clog" "W" "INTEGER" "integer" 6 true
"ng_sv_top.u_clog" "NAME" "STRING" "string" "def" false
"ng_sv_top.u_clog" "V" "STD_LOGIC_VECTOR" "bits" "1001" true
"ng_sv_top.u_clog" "B" "BOOLEAN" "boolean" true true
"ng_sv_top.u_clog" "R" "REAL" "real" 0.5 false
"ng_sv_top.u_def" "W" "INTEGER" "integer" 4 false
"ng_sv_top.u_def" "NAME" "STRING" "string" "def" false
"ng_sv_top.u_def" "V" "STD_LOGIC_VECTOR" "bits" "1010" false
"ng_sv_top.u_def" "B" "BOOLEAN" "boolean" false false
"ng_sv_top.u_def" "R" "REAL" "real" 0.5 false
EOT

diff -u want.txt got.txt

# Backward compatible: the old parameters are still written
grep -q '^"parameters":{' out.json
