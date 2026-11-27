set -xe

# An SV island domain detects a clock or reset edge when the signal
# changes, as an always process does: a zero-width pulse (a change and a
# change back in one region) is an edge, a change that is not an edge adds
# no delta cycle, so the waves of a $finish in that time step keep its last
# changes.  Islands must match NVC_RTL_ISLANDS=off exactly.

cat >edge1.sv <<'EOF'
module edge1;
  logic clk = 0, rst_n = 1;
  logic [7:0] d = 1, q = 0, n = 0, r = 0;

  always_ff @(posedge clk or negedge rst_n)   // Island domains
    if (!rst_n) q <= 8'h80;
    else q <= q + d;
  always_ff @(negedge clk) n <= n + 1;
  always_ff @(posedge rst_n) r <= r + 1;

  initial begin
    #1 clk = 1; clk = 0;          // Zero-width clock pulse
    #1 $display("1: q=%h n=%0d r=%0d", q, n, r);
    #1 rst_n = 0; rst_n = 1;      // Zero-width reset pulse
    #1 $display("2: q=%h n=%0d r=%0d", q, n, r);
    #1 clk = 1;
    #1 clk = 0; d = 7;            // Last changes at the $finish time
    $display("3: q=%h n=%0d r=%0d", q, n, r);
    $finish;
  end
endmodule
EOF

strip_date() {
   sed '/^\$date/,/^\$end/d' "$1" > "$2"
}

run_mode() {
   mkdir -p $1
   ( cd $1
     export NVC_RTL_ISLANDS=$2 NVC_ISLAND_FAST=$3
     nvc -a ../edge1.sv -e edge1
     NVC_DUMP_ISLANDS=1 nvc -r --wave=w.vcd --format=vcd edge1 >out.txt \
                     2>dump.txt
     nvc -r --wave=w.fst edge1 >/dev/null 2>&1
     strip_date w.vcd w.norm
     if command -v fst2vcd >/dev/null 2>&1; then
        fst2vcd -f w.fst | sed '/^\$date/,/^\$end/d' > f.norm
     fi
     grep -v '^ISLAND' out.txt > out.norm
     grep 'finish called' dump.txt > finish.txt )
}

run_mode fast hier 1
run_mode nofast hier 0
run_mode off off 0

grep -q 'ISLAND_HIER: islands=[1-9]' fast/out.txt

for m in fast nofast; do
   diff -u off/out.norm $m/out.norm
   diff -u off/w.norm $m/w.norm
   diff -u off/finish.txt $m/finish.txt
   if [ -f off/f.norm ]; then
      diff -u off/f.norm $m/f.norm
   fi
done

grep -q '1: q=01 n=1 r=0' off/out.norm
grep -q '2: q=02 n=1 r=1' off/out.norm
grep -q '3: q=03 n=1 r=1' off/out.norm
