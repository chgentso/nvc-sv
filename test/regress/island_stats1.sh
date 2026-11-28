set -xe

# NVC_ISLAND_STATS=1: evaluation census of the islands at the end of the run
# (evaluations, repeats in one delta / time step, direct wires, flip-flop
# samples, busiest module bodies).  Diagnostics only: the simulation output
# is the same with and without it.

cat >stats1.sv <<'EOF2'
module stats1_inc (input logic [7:0] a, output logic [7:0] y);
  assign y = a + 8'd1;
endmodule

module stats1_reg (input logic clk, input logic rst, input logic [7:0] d,
                   output logic [7:0] q);
  always_ff @(posedge clk) q <= rst ? 8'd0 : d;
endmodule

module stats1;
  logic clk = 0, rst = 1;
  logic [7:0] q, y1, y2;

  stats1_reg u_reg (.clk, .rst, .d(y2), .q);
  stats1_inc u_inc1 (.a(q), .y(y1));
  stats1_inc u_inc2 (.a(y1), .y(y2));

  always #5 clk = ~clk;

  initial begin
    #12 rst = 0;
    #188 $display("q=%0d", q);
    $finish;
  end
endmodule
EOF2

export NVC_RTL_ISLANDS=hier
nvc -a stats1.sv -e stats1

nvc -r stats1 >plain.txt 2>&1
! grep -q ISLAND_STATS plain.txt

NVC_ISLAND_STATS=1 nvc -r stats1 >stats.txt 2>&1
cat stats.txt
grep -E '^ISLAND_STATS: 3 islands \(3 in 1 clusters, 2 evaluated\)' stats.txt
grep -E '^ISLAND_STATS: comb evaluations [1-9][0-9]+ in [1-9][0-9]+ time steps' stats.txt
grep -q '^q=38$' stats.txt
grep -E '^ISLAND_STATS: repeats: same delta [0-9]+' stats.txt
grep -E '^ISLAND_STATS: per evaluation: [0-9.]+ direct wires copied' stats.txt
grep -E '^ISLAND_STATS: flip-flops: [0-9]+ super-domain edges sampling [1-9][0-9]* domains' stats.txt
grep -E '^ISLAND_STATS: .* 2 inst \(2 leaf\) .*STATS1_INC' stats.txt

grep -v '^ISLAND_STATS' stats.txt >stripped.txt
diff -u plain.txt stripped.txt
