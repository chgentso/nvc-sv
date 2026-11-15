set -xe

# Clock evidence of Verilog processes: --netlist --evidence writes the
# clocks, asynchronous controls and per-output dependencies of every
# $process cell and the attributes of nets and modules

which nvc

nvc --std=19 -a $TESTDIR/netlist/ev_sv1.sv -e ev_sv1

nvc --std=19 --netlist ev_sv1 --output=plain.json
[ $(grep -c '"evidence' plain.json) = 0 ]
[ $(grep -c '"async_reg"' plain.json) = 0 ]
[ $(grep -c '"keep_hierarchy"' plain.json) = 0 ]

nvc --std=19 --netlist --evidence ev_sv1 --output=ev.json

grep -q '^"evidence_version":1' ev.json

# One line for each process: module, label and the flattened evidence
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

# Every process cell has evidence
[ $(grep -c '"cell":"\$process"' ev.json) = $(wc -l < ev.txt) ]

# ev MODULE LABEL FRAGMENT...: the process has all the fragments
ev() {
  m=$1; l=$2; shift 2
  [ $(grep -cF "$m|$l|" ev.txt) = 1 ]
  for f in "$@"; do grep -F "$m|$l|" ev.txt | grep -qF -- "$f"; done
}
notev() {
  m=$1; l=$2; f=$3
  [ $(grep -cF "$m|$l|" ev.txt) = 1 ]
  ! grep -F "$m|$l|" ev.txt | grep -qF -- "$f"
}

# Asynchronous reset low, enable controls only one output, copy
ev ev_sv1 'always_ff @33' '"class":"seq"' '"confidence":"exact"' \
   '"clocks":[{"pin":"clk_a","edge":"rise"}]' \
   '"async":[{"pin":"rst_n","level":"low","role":"reset"}]' \
   '"ra":{"class":"seq","clock":0,"data":["din"],"ctrl":[],"copy":"din"}' \
   "\"t_a'\":{\"class\":\"seq\",\"clock\":0,\"data\":[\"t_a\"],\"ctrl\":[\"en\"]}"

# Parenthesised edges, set to one with a high active control
ev ev_sv1 'always @38' '"clocks":[{"pin":"clk_b","edge":"rise"}]' \
   '"async":[{"pin":"rst_set","level":"high","role":"set"}]' \
   '"copy":"ra[0]"'

# Falling edge, control tested by comparison
ev ev_sv1 'always @43' '"clocks":[{"pin":"clk_b","edge":"fall"}]' \
   '"async":[{"pin":"rst_n","level":"low","role":"reset"}]'

# iff is control, not data
ev ev_sv1 'always_ff @47' '"clocks":[{"pin":"clk_b","edge":"rise"}]' \
   '"async":[]' '"data":["sel","din[1]"],"ctrl":["en"]'
notev ev_sv1 'always_ff @47' '"copy"'

# Synchronous reset
ev ev_sv1 'always_ff @50' '"sync_reset":["srst"]' '"async":[]' \
   '"data":["a3"],"ctrl":["srst"]'

# Blocking temporary: its sources reach the output
ev ev_sv1 'always_ff @56' '"a5":{"class":"seq","clock":0,"data":["a3","a4"]'

# Both edges, slice copy, two clocks
ev ev_sv1 'always @62' '"clocks":[{"pin":"clk_a","edge":"both"}]' \
   '"copy":"ra[2]"'
ev ev_sv1 'always @65' \
   '"clocks":[{"pin":"clk_a","edge":"rise"},{"pin":"clk_b","edge":"rise"}]'

# Combinational and latch processes
ev ev_sv1 'always_comb @68' '"class":"comb"' '"clocks":[]' \
   '"o3":{"class":"comb","data":["sel","a1","a2"],"ctrl":[]}'
ev ev_sv1 'always @69' '"class":"comb"' '"clocks":[]'
ev ev_sv1 'always_latch @70' '"class":"latch"' \
   '"lat":{"class":"latch","data":["din[3]"],"ctrl":["en"]'

# A select that depends on a signal is an approximation
ev ev_sv1 'always @72' '"confidence":"approx"'

# Continuous assignments and gates
ev ev_sv1 'assign @74' '"class":"comb"' '"data":["clk_a","gen_en"]'
notev ev_sv1 'assign @74' '"copy"'
ev ev_sv1 'assign @76' '"class":"comb"' '"copy":"rb"'
ev ev_sv1 'gate @77' '"class":"comb"' '"copy":"ra[1]"'

# Generate loop and instance arrays: each instance has its own cells
for u in 'g[0].u' 'g[1].u' 'g[2].u' 'g[3].u'; do
  ev "ev_sv1.$u" 'always_ff @5' '"clocks":[{"pin":"clk","edge":"rise"}]' \
     "\"s0'\":{\"class\":\"seq\",\"clock\":0,\"data\":[\"d\"]" \
     '"s1":{"class":"seq","clock":0,"data":["s0"],"ctrl":[],"copy":"s0"}'
  ev "ev_sv1.$u" 'assign @9' '"copy":"s1"'
done
for u in 'ua[0]' 'ua[1]' up; do
  ev "ev_sv1.$u" 'always_ff @14' '"clocks":[{"pin":"clk","edge":"rise"}]'
done

# Attributes of modules and nets: lower case names, the value as written, 1
# without a value
grep -q '^"keep_hierarchy":"yes"' ev.json
grep -q '^"cdc_group":"g1"' ev.json
[ $(grep -c '^"async_reg":"TRUE"' ev.json) = 8 ]
[ $(grep -c '^"async_reg":"1"' ev.json) = 1 ]

# The top level module has a source location
grep -q '^"src":".*ev_sv1.sv:22"' ev.json

# More shapes: parameter reset, set and reset, calls, loops, bench-like
nvc --std=19 -a $TESTDIR/netlist/ev_sv2.sv -e ev_sv2
nvc --std=19 --netlist --evidence ev_sv2 --output=ev2.json
evlines ev2.json > ev.txt

ev ev_sv2 'always @18' '"async":[{"pin":"rst","level":"high","role":"unknown"}]' \
   '"ctrl":["op"]' '"confidence":"exact"'
ev ev_sv2 'always @31' \
   '"async":[{"pin":"rst","level":"high","role":"reset"},{"pin":"set_n","level":"low","role":"set"}]' \
   "\"acc'\":{\"class\":\"seq\",\"clock\":0,\"data\":[\"a\",\"acc\"],\"ctrl\":[]}"
ev ev_sv2 'always @37' '"confidence":"approx"' '"data":["a","b"]'
ev ev_sv2 'always @40' '"confidence":"approx"' '"data":["a","b"]'
ev ev_sv2 'always @43' '"clocks":[{"pin":"clk","edge":"rise"}]'
ev ev_sv2 'always @46' '"gated":{"class":"seq","clock":0,"data":["clk","op[0]"]'
ev ev_sv2 'initial @49' '"class":"other"'
ev ev_sv2 'always @50' '"class":"other"'
ev ev_sv2 'always @51' '"class":"other"'
notev ev_sv2 'always @51' '"outputs"'
ev ev_sv2 'assign @54' '"data":["r1[3:0]","r2[3:0]"]'
