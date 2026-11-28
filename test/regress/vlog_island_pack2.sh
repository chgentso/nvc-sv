set -xe

# Packed SystemVerilog island clusters (NVC_ISLAND_PACK): member states in
# one block in topological order and the cluster dirty bitmap, with clock
# gating cell copies feeding counters, a waveform dump, and speculative
# variants on and off.  Output and dump must be the same with
# NVC_ISLAND_PACK=0 and with --rtl-islands=off.

cat >pack2.sv <<'EOF2'
module pack2_gate (input clk_in, input en, output clk_out);
  wire en_l = en;
  assign clk_out = clk_in;
endmodule

module pack2_cnt #(parameter INC = 1) (input clk, input rst_n,
                                       output logic [7:0] q);
  always @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 8'h00;
    else q <= q + INC;
endmodule

module pack2_mix (input logic [7:0] a, input logic [7:0] b,
                  output logic [7:0] y);
  assign y = (a ^ b) + 8'd7;
endmodule

module pack2;
  logic clk = 0, rst_n = 0;
  wire c1, c2, c3;
  wire [7:0] q1, q2, q3, m1, m2, m3;

  pack2_gate g1 (.clk_in(clk), .en(1'b1), .clk_out(c1));
  pack2_gate g2 (.clk_in(c1), .en(1'b1), .clk_out(c2));
  pack2_gate g3 (.clk_in(clk), .en(1'b1), .clk_out(c3));
  pack2_cnt #(1) u1 (.clk(c1), .rst_n, .q(q1));
  pack2_cnt #(3) u2 (.clk(c2), .rst_n, .q(q2));
  pack2_cnt #(5) u3 (.clk(c3), .rst_n, .q(q3));
  pack2_mix x1 (.a(q1), .b(q2), .y(m1));
  pack2_mix x2 (.a(m1), .b(q3), .y(m2));
  pack2_mix x3 (.a(m2), .b(m1), .y(m3));

  always #5 clk = ~clk;
  always @(posedge clk) if (m3[1:0] == 2'b10) $display("%0t m3=%0d", $time, m3);

  initial begin
    #12 rst_n = 1;
    #140 $display("q=%0d,%0d,%0d m3=%0d", q1, q2, q3, m3);
    $finish;
  end
endmodule
EOF2

strip_date() {
  sed -e '/^\$date/,/\$end/d' "$1" > "$2"
}

export NVC_RTL_ISLANDS=hier
nvc --work=w0 -a pack2.sv -e pack2
NVC_ISLAND_SPEC=1 nvc --work=w1 -a pack2.sv -e pack2
NVC_ISLAND_STATS=1 nvc --work=w0 -r pack2 >stats.txt 2>&1
grep -E '^ISLAND_STATS: 9 islands \(3 in 1 clusters' stats.txt

run() {
  local tag=$1; shift
  env "$@" nvc --work=$W -r pack2 --wave=pack2.vcd --format=vcd >$tag.txt 2>&1
  strip_date pack2.vcd $tag.norm
}

for spec in 0 1; do
  W=w$spec
  run p$spec NVC_ISLAND_SPEC=$spec
  run n$spec NVC_ISLAND_SPEC=$spec NVC_ISLAND_PACK=0
  run o$spec NVC_ISLAND_SPEC=$spec NVC_RTL_ISLANDS=off
  diff -u n$spec.txt p$spec.txt
  diff -u o$spec.txt p$spec.txt
  diff -u n$spec.norm p$spec.norm
  diff -u o$spec.norm p$spec.norm
done
diff -u p0.txt p1.txt
grep -q '^q=14,42,70 m3=102$' p0.txt
