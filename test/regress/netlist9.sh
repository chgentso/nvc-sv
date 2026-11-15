set -xe

# Clock evidence across the language boundary: Verilog modules below a VHDL
# top level and around a VHDL entity

which nvc

nvc --std=08 -a $TESTDIR/netlist/ev_svmix_leaf.vhd \
    $TESTDIR/netlist/ev_svmix.v $TESTDIR/netlist/ev_svmix.vhd -e ev_svmix

nvc --std=08 --netlist ev_svmix --output=plain.json
[ $(grep -c '"evidence' plain.json) = 0 ]
[ $(grep -c '"async_reg"' plain.json) = 0 ]

nvc --std=08 --netlist --evidence ev_svmix --output=ev.json

grep -q '^"evidence_version":1' ev.json
[ $(grep -c '"cell":"\$process"' ev.json) = 4 ]
[ $(grep -c '^"evidence":{' ev.json) = 4 ]

# The VHDL entity inside the Verilog module has evidence of its own
grep -q '^"EV_SVMIX.U_TOP.u_leaf":{' ev.json
grep -q '^"unit":"EV_VLEAF"' ev.json

# The Verilog module below the VHDL top level
evlines() {
  awk '
    /^"attributes":\{/ { if (pend != "") { mod=pend; sub(/":\{$/, "", mod);
                                            sub(/^"/, "", mod) } }
    { pend = ($0 ~ /^"[^"]*":\{$/) ? $0 : "" }
    /^"label":"/ { lab=$0; sub(/^"label":"/, "", lab); sub(/",?$/, "", lab) }
    /^"evidence":\{/ { buf=""; inev=1 }
    /^"port_directions":/ { if (inev) print mod "|" lab "|" buf; inev=0 }
    inev { buf = buf $0 }
  ' "$1"
}

evlines ev.json > ev.txt

ev() {
  m=$1; l=$2; shift 2
  [ $(grep -cF "$m|$l|" ev.txt) = 1 ]
  for f in "$@"; do grep -F "$m|$l|" ev.txt | grep -qF -- "$f"; done
}

ev EV_SVMIX.U_TOP 'always @14' '"class":"seq"' \
   '"clocks":[{"pin":"clk_a","edge":"rise"}]' '"copy":"d"'
ev EV_SVMIX.U_TOP.u_sync 'always @4' '"class":"seq"' \
   '"clocks":[{"pin":"clk","edge":"rise"}]' \
   "\"s0'\":{\"class\":\"seq\",\"clock\":0,\"data\":[\"d\"],\"ctrl\":[],\"copy\":\"d\"}" \
   '"s1":{"class":"seq","clock":0,"data":["s0"],"ctrl":[],"copy":"s0"}'
ev EV_SVMIX.U_TOP.u_sync 'assign @8' '"class":"comb"' '"copy":"s1"'
grep -F 'EV_SVMIX.U_TOP.u_leaf|' ev.txt | grep -q '"class":"'

# Attributes of the nets of the Verilog module under the VHDL top level
[ $(grep -c '^"async_reg":"TRUE"' ev.json) = 2 ]
