set -xe

# Clock copies (vlog_island_clkcopy1) whose islands have speculative
# two-state variants (NVC_ISLAND_SPEC=1): the leader runs its variant and
# keeps the other copies, which promote, take x and z from the leader
# while kept, and run again for another input (en) with the unknown value
# in their state.  The leader's island also deopts on an x enable.  A force
# of a copy dissolves the group, one of the enable keeps it.
# Results and waves must be those of --rtl-islands=off and
# NVC_ISLAND_CLK_COPY=0, with and without the variants.

cat >clkcopy_spec1.sv <<'EOF'
module clkcopy_spec1_gate (input clk_in, input en, input se,
                           output clk_out, output en_q);
  assign en_q = en ^ se;
  assign clk_out = clk_in;
endmodule

module clkcopy_spec1_cnt #(parameter INC = 1) (input clk, input rst_n,
                                               output logic [7:0] q);
  always @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 8'h00;
    else q <= q + INC;
endmodule

module clkcopy_spec1;
  logic clk = 0, rst_n = 1, en = 1;
  wire c1, c2, c3, c4, e1, e2, e3, e4;
  wire [7:0] q1a, q1b, q2a, q2b, q3a, q3b, q4;

  clkcopy_spec1_gate g1 (.clk_in(clk), .en, .se(1'b0), .clk_out(c1), .en_q(e1));
  clkcopy_spec1_gate g2 (.clk_in(clk), .en, .se(1'b0), .clk_out(c2), .en_q(e2));
  clkcopy_spec1_gate g3 (.clk_in(c1), .en, .se(1'b0), .clk_out(c3), .en_q(e3));
  clkcopy_spec1_gate g4 (.clk_in(c3), .en, .se(1'b0), .clk_out(c4), .en_q(e4));

  clkcopy_spec1_cnt #(1) u1a (.clk(c1), .rst_n, .q(q1a));
  clkcopy_spec1_cnt #(2) u1b (.clk(c1), .rst_n, .q(q1b));
  clkcopy_spec1_cnt #(3) u2a (.clk(c2), .rst_n, .q(q2a));
  clkcopy_spec1_cnt #(4) u2b (.clk(c2), .rst_n, .q(q2b));
  clkcopy_spec1_cnt #(5) u3a (.clk(c3), .rst_n, .q(q3a));
  clkcopy_spec1_cnt #(6) u3b (.clk(c3), .rst_n, .q(q3b));
  clkcopy_spec1_cnt #(7) u4 (.clk(c4), .rst_n, .q(q4));

  task show(input string tag);
    $display("%0t %s q1=%h/%h q2=%h/%h q3=%h/%h q4=%h e=%b%b%b%b", $time,
             tag, q1a, q1b, q2a, q2b, q3a, q3b, q4, e1, e2, e3, e4);
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
    clocks(40);                   // The islands promote
    show("a");
    #1 clk = 1'bx;                // x through the kept copies
    #1 show("x");
    #1 en = 0;                    // The copies' islands run with clk x
    #1 en = 1;
    #1 show("y");
    #1 clk = 1;                   // x -> 1: an edge
    #1 clk = 0;
    #1 en = 0;
    #1 en = 1;
    #1 show("b");
    clocks(40);                   // Back to "$a"
    #1 en = 1'bx;                 // The leader's island reads x
    clocks(2);
    #1 clk = 1'bz;
    #1 en = 0;
    #1 show("z");
    #1 clk = 0;
    #1 en = 1;
    clocks(40);
    show("c");
    if ($test$plusargs("nested"))
      force c3 = 1'bx;            // A copy of a copy
    else if ($test$plusargs("copy"))
      force c2 = 1'bx;            // Dissolves the group
    else if ($test$plusargs("fen"))
      force en = 1'bx;            // Keeps the group
    clocks(2);
    #1 en = 0;
    #1 show("f");
    if ($test$plusargs("nested"))
      release c3;
    else if ($test$plusargs("copy"))
      release c2;
    else if ($test$plusargs("fen"))
      release en;
    #1 en = 1;
    clocks(40);
    show("g");
    #5 clk = 1;
    $finish;
  end
endmodule
EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_HOLD_WAIT NVC_ISLAND_CLK_COPY
unset NVC_RTL_ISLANDS NVC_ISLAND_FAST

nd() { sed '/^\$date/,/^\$end/d' $1; }
nodebug() { grep -v '^\*\* Debug' || true; }   # Debug builds: JIT footprint

nvc --work=w_off -a clkcopy_spec1.sv
nvc --work=w_off --rtl-islands=off -e clkcopy_spec1
for p in plain nested copy fen; do
   nvc --work=w_off -r --wave=off_$p.vcd --format=vcd clkcopy_spec1 +$p \
       2>&1 | nodebug > off_$p.log
done

nvc --work=w_n -a clkcopy_spec1.sv
nvc --work=w_n -e clkcopy_spec1
NVC_ISLAND_SPEC=1 nvc --work=w_s -a clkcopy_spec1.sv
NVC_ISLAND_SPEC=1 nvc --work=w_s -e clkcopy_spec1

run() {   # run <work> <mode> <variant>
   env $2 NVC_DUMP_ISLANDS=1 NVC_ISLAND_SPEC_STATS=1 nvc --work=$1 -r \
       --wave=s.vcd --format=vcd clkcopy_spec1 +$3 2>&1 | nodebug > raw.log
   grep -v '^ISLAND' raw.log > s.log
   diff -u off_$3.log s.log
   diff -u <(nd off_$3.vcd) <(nd s.vcd)
   cp raw.log "${1}_${2:-default}_$3.log"
}

for mode in "" NVC_ISLAND_CLK_COPY=0; do
   for p in plain copy; do run w_n "$mode" $p; done
done
for mode in "" NVC_ISLAND_CLK_COPY=0 NVC_ISLAND_SPEC_FORCE=A \
            NVC_ISLAND_SPEC_STRESS=3; do
   for p in plain copy; do run w_s "$mode" $p; done
done
run w_s "" nested
run w_s "" fen

# The variants ran and the leader kept the copies with them (the forces
# dissolve the group)
for m in w_n_default w_s_default w_s_NVC_ISLAND_SPEC_FORCE=A \
         w_s_NVC_ISLAND_SPEC_STRESS=3; do
   grep -q '^ISLAND_CLK_COPY: kept=[1-9] .* broken=0' ${m}_plain.log
   grep -q '^ISLAND_CLK_COPY: kept=0 .* broken=1' ${m}_copy.log
done
grep -q '^ISLAND_CLK_COPY: kept=0 .* broken=1' w_s_default_nested.log
grep -q '^ISLAND_CLK_COPY: kept=[1-9] .* broken=0' w_s_default_fen.log
! grep -q '^ISLAND_CLK_COPY: kept=[1-9]' w_s_NVC_ISLAND_CLK_COPY=0_plain.log
grep -q '^ISLAND_SPEC_RT: sv variants=[1-9][0-9]* promoted=[1-9]' \
     w_s_default_plain.log
grep -q '^ISLAND_SPEC_RT: deopts .* unclean=[1-9]' \
     w_s_NVC_ISLAND_SPEC_FORCE=A_plain.log
# The x clock reached the counters
grep -q '^404 x q1=29/52 q2=7b/a4 q3=cd/f6 q4=1f e=1111' off_plain.log
