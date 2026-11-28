set -xe

# Dirty bitmap of SystemVerilog island clusters (NVC_ISLAND_PACK, default
# on): a cluster of 72 instances (two bitmap words and a partial third)
# with a forward path 0 -> 71 and a backward path 71 -> 0 through every
# instance, so passes mark members at earlier positions dirty and leave
# them for the next pass; a flip-flop closing the forward path; a force and
# release of a wire inside the chain; values printed on every clock edge.
# Output and VCD must be the same with NVC_ISLAND_PACK=0, with
# --no-island-fast and with --rtl-islands=off.

N=72
{
  cat <<'EOF2'
module pk_cell (input logic [7:0] a, input logic [7:0] k,
                output logic [7:0] y, output logic [7:0] w);
  assign y = a + 8'd1 + {7'd0, k[0]};
  assign w = k ^ 8'h5a;
endmodule

module pk_ff (input logic clk, input logic [7:0] d, output logic [7:0] q);
  always_ff @(posedge clk) q <= d;
endmodule

module vlog_island_pack1;
  logic clk = 0;
  logic [7:0] cnt = 8'd0;
  wire [7:0] q;
EOF2
  for i in $(seq 0 $N); do echo "  wire [7:0] y$i, w$i;"; done
  for i in $(seq 0 $((N - 1))); do
    a=$([ $i = 0 ] && echo q || echo y$((i - 1)))
    k=$([ $i = $((N - 1)) ] && echo cnt || echo w$((i + 1)))
    echo "  pk_cell u$i (.a($a), .k($k), .y(y$i), .w(w$i));"
  done
  cat <<EOF2
  pk_ff uf (.clk, .d(y$((N - 1))), .q);

  always #5 clk = ~clk;
  always @(posedge clk) \$display("%0t q=%0d y40=%0d w0=%0d", \$time, q, y40, w0);

  initial begin
    #23 cnt = 8'd3;
    #20 cnt = 8'd8;
    #20 force y35 = 8'haa;
    #20 cnt = 8'd1;
    #20 release y35;
    #20 cnt = 8'd2;
    #40 \$finish;
  end
endmodule
EOF2
} > pack1.sv

strip_date() {
  sed -e '/^\$date/,/\$end/d' "$1" > "$2"
}

export NVC_RTL_ISLANDS=hier
nvc -a pack1.sv -e vlog_island_pack1

NVC_ISLAND_STATS=1 nvc -r vlog_island_pack1 >stats.txt 2>&1
grep -E "^ISLAND_STATS: $((N + 1)) islands \($((N + 1)) in 1 clusters" stats.txt

nvc -r vlog_island_pack1 --wave=pack.vcd --format=vcd >pack.txt 2>&1
NVC_ISLAND_PACK=0 nvc -r vlog_island_pack1 --wave=nopack.vcd --format=vcd \
  >nopack.txt 2>&1
nvc -r --no-island-fast vlog_island_pack1 >nofast.txt 2>&1
NVC_RTL_ISLANDS=off nvc -r vlog_island_pack1 --wave=off.vcd --format=vcd \
  >off.txt 2>&1
nvc -r vlog_island_pack1 >plain.txt 2>&1

cat pack.txt
grep -q '^155 q=234 y40=19 w0=2$' pack.txt
diff -u nopack.txt pack.txt
diff -u nofast.txt pack.txt
diff -u off.txt pack.txt
diff -u plain.txt pack.txt
for f in pack nopack off; do strip_date $f.vcd $f.norm; done
diff -u nopack.norm pack.norm
diff -u off.norm pack.norm
