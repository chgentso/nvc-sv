set -xe

# Domains clocked by copies of one clock (assign clk_out = clk_in, as in
# clock gating cell models, and copies of copies) share super-domains
# watching one copy, and after time zero the copies that only copy are
# kept by that copy's island instead of running.  Zero-width pulses, Z and
# X, forces of the root clock and of a copy (which dissolves the group),
# with and without waves, must give the same results and waves as
# NVC_RTL_ISLANDS=off and NVC_ISLAND_CLK_COPY=0.

cat >clkcopy1.sv <<'EOF'
module clkcopy1_gate (input clk_in, input en, input se, output clk_out);
  wire en_latch, scan;
  assign en_latch = en | se;      // Unused, as in clock gating cell models
  assign scan = se;
  assign clk_out = clk_in;
endmodule

module clkcopy1_cnt #(parameter INC = 1) (input clk, input rst_n,
                                          output logic [7:0] q);
  always @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 8'h00;
    else q <= q + INC;
endmodule

module clkcopy1_ncnt (input clk, output logic [7:0] q = 8'h40);
  always @(negedge clk) q <= q + 1;
endmodule

module clkcopy1;
  logic clk = 0, rst_n = 1, en = 1;
  wire c1, c2, c3, c4;
  wire [7:0] q1a, q1b, q2a, q2b, q3a, q3b, q4, n2, qr;

  clkcopy1_gate g1 (.clk_in(clk), .en, .se(1'b0), .clk_out(c1));
  clkcopy1_gate g2 (.clk_in(clk), .en, .se(1'b0), .clk_out(c2));
  clkcopy1_gate g3 (.clk_in(c1), .en, .se(1'b0), .clk_out(c3));
  clkcopy1_gate g4 (.clk_in(c3), .en, .se(1'b0), .clk_out(c4));

  clkcopy1_cnt #(1) u1a (.clk(c1), .rst_n, .q(q1a));
  clkcopy1_cnt #(2) u1b (.clk(c1), .rst_n, .q(q1b));
  clkcopy1_cnt #(3) u2a (.clk(c2), .rst_n, .q(q2a));
  clkcopy1_cnt #(4) u2b (.clk(c2), .rst_n, .q(q2b));
  clkcopy1_cnt #(5) u3a (.clk(c3), .rst_n, .q(q3a));
  clkcopy1_cnt #(6) u3b (.clk(c3), .rst_n, .q(q3b));
  clkcopy1_cnt #(7) u4 (.clk(c4), .rst_n, .q(q4));
  clkcopy1_cnt #(9) ur (.clk(clk), .rst_n, .q(qr));
  clkcopy1_ncnt un2 (.clk(c2), .q(n2));

  always @(posedge c4) $display("%0t c4 q4=%h q1a=%h", $time, q4, q1a);

  task show(input string tag);
    $display("%0t %s q1=%h/%h q2=%h/%h q3=%h/%h q4=%h n2=%h qr=%h", $time,
             tag, q1a, q1b, q2a, q2b, q3a, q3b, q4, n2, qr);
  endtask

  task clocks(input int n);
    repeat (n) begin
      #5 clk = 1;
      #5 clk = 0;
    end
  endtask

  initial begin
    #1 rst_n = 0;
    #1 rst_n = 1;
    clocks(4);
    show("a");
    #1 clk = 1; clk = 0;          // Zero-width pulse: no edge on the copies
    #1 show("b");
    #1 clk = 1; #0 clk = 0;       // Pulse over two delta cycles
    #1 show("c");
    #1 clk = 1'bz;                // Z passes through a copy
    #1 show("z");
    #1 clk = 0;
    #1 clk = 1'bx;
    #1 clk = 1;
    #1 clk = 0;
    #1 show("x");
    en = 0;                       // The copies' islands run for en
    clocks(1);
    en = 1;
    force clk = 1'b1;             // The root clock: the copies follow
    #1 show("d");
    clocks(2);
    release clk;
    #1 show("e");
    clocks(2);
    if ($test$plusargs("nested"))
      force c3 = 1'b1;            // A copy of a copy
    else
      force c2 = 1'b1;
    #1 show("f");
    clocks(3);
    show("g");
    release c2;
    release c3;
    #1 show("h");
    clocks(3);
    show("i");
    #5 clk = 1;
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
     export NVC_RTL_ISLANDS=$2 NVC_ISLAND_FAST=$3 NVC_ISLAND_CLK_COPY=$4
     nvc -a ../clkcopy1.sv -e clkcopy1
     for p in plain nested; do
        NVC_DUMP_ISLANDS=1 nvc -r clkcopy1 +$p >$p.txt 2>$p.err
        grep -v '^ISLAND' $p.txt > $p.out
        nvc -r --wave=$p.vcd --format=vcd clkcopy1 +$p >$p.wout 2>/dev/null
        strip_date $p.vcd $p.norm
     done )
}

run_mode fast hier 1 1
run_mode nofast hier 0 1
run_mode nocopy hier 1 0
run_mode off off 0 1

grep -q 'ISLAND_CLK_COPY: copies=8 grouped=4 groups=1 domains=8 kept=3' fast/plain.err

for m in fast nofast nocopy; do
   for p in plain nested; do
      diff -u off/$p.out $m/$p.out
      diff -u off/$p.out $m/$p.wout
      diff -u off/$p.norm $m/$p.norm
   done
done
