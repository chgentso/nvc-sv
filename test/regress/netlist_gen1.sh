set -xe

# Elaborated generics of VHDL instances with the type, how to read the value and whether the instance gave it or the default was used: integer, string, vector, enumeration, boolean, real, time and natural generics, in a generate loop too

nvc --std=08 -a $TESTDIR/netlist/ng_vhdl.vhd -e ng_vhdl
nvc --std=08 --netlist ng_vhdl --output=out.json
awk -f $TESTDIR/regress/ng_extract.awk out.json > got.txt

cat >want.txt <<'EOT'
"NG_VHDL.U_ALL" "W" "INTEGER" "integer" 4 true
"NG_VHDL.U_ALL" "NAME" "STRING" "string" "xy\"z" true
"NG_VHDL.U_ALL" "V" "STD_LOGIC_VECTOR" "bits" "0011" true
"NG_VHDL.U_ALL" "E" "STD_LOGIC" "enum" "'1'" false
"NG_VHDL.U_ALL" "B" "BOOLEAN" "boolean" true true
"NG_VHDL.U_ALL" "R" "REAL" "real" 0.001 true
"NG_VHDL.U_ALL" "T" "TIME" "time" "2 us" true
"NG_VHDL.U_ALL" "N" "NATURAL" "integer" 2 false
"NG_VHDL.U_DEF" "W" "INTEGER" "integer" 4 false
"NG_VHDL.U_DEF" "NAME" "STRING" "string" "def" false
"NG_VHDL.U_DEF" "V" "STD_LOGIC_VECTOR" "bits" "1010" false
"NG_VHDL.U_DEF" "E" "STD_LOGIC" "enum" "'1'" false
"NG_VHDL.U_DEF" "B" "BOOLEAN" "boolean" false false
"NG_VHDL.U_DEF" "R" "REAL" "real" 0.5 false
"NG_VHDL.U_DEF" "T" "TIME" "time" "10 ns" false
"NG_VHDL.U_DEF" "N" "NATURAL" "integer" 2 false
"NG_VHDL.G(0).U" "W" "INTEGER" "integer" 5 true
"NG_VHDL.G(0).U" "NAME" "STRING" "string" "def" false
"NG_VHDL.G(0).U" "V" "STD_LOGIC_VECTOR" "bits" "1010" false
"NG_VHDL.G(0).U" "E" "STD_LOGIC" "enum" "'1'" false
"NG_VHDL.G(0).U" "B" "BOOLEAN" "boolean" false false
"NG_VHDL.G(0).U" "R" "REAL" "real" 0.5 false
"NG_VHDL.G(0).U" "T" "TIME" "time" "10 ns" false
"NG_VHDL.G(0).U" "N" "NATURAL" "integer" 0 true
"NG_VHDL.G(1).U" "W" "INTEGER" "integer" 6 true
"NG_VHDL.G(1).U" "NAME" "STRING" "string" "def" false
"NG_VHDL.G(1).U" "V" "STD_LOGIC_VECTOR" "bits" "1010" false
"NG_VHDL.G(1).U" "E" "STD_LOGIC" "enum" "'1'" false
"NG_VHDL.G(1).U" "B" "BOOLEAN" "boolean" false false
"NG_VHDL.G(1).U" "R" "REAL" "real" 0.5 false
"NG_VHDL.G(1).U" "T" "TIME" "time" "10 ns" false
"NG_VHDL.G(1).U" "N" "NATURAL" "integer" 3 true
EOT

diff -u want.txt got.txt

# Backward compatible: the old parameters are still written
grep -q '^"parameters":{' out.json
