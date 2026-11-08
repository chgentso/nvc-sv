`timescale 1ns/1ns
// Checkers (17): instantiation with default and explicit arguments, checker
// variables updated by always_ff, assert/cover property inside checkers with
// the checker's default clocking and with an explicit clock, function in a
// checker called hierarchically.
// Expected values: IEEE 1800-2017 17.2 (checker declaration), 17.3
// (instantiation, argument defaults), 17.5 (checker procedures), 16.3 (assert
// else action), 16.9.3 (sampling at the clock tick), 11.8 (unsigned
// compare of v with lim).
//
// Stimulus: clk_g has posedges at 5, 15, 25, ... (checked values sampled in the
// preponed region); a, b, val change at the negedges 10, 20, 30, 40.
//   sampled at posedge: t=5 : a=0 val=0
//                       t=15: a=1 val=2
//                       t=25: a=0 val=4
//                       t=35: a=1 val=6
//                       t=45...: a=0 val=1
// chk_val fails when v >= lim: lim 5 -> t=35; lim 3 -> t=25 and t=35;
// lim 10 (default) never.  chk_edge counts the ticks where a is 1: cnt = 2.
// chk_pair: a |=> b has no failure (b is 1 at every tick after 10) and
// a ##1 b is covered once per attempt: attempts at 15 and 35.
// The order of the two failure actions at t=35 is not checked.
int lim_n[16];
longint lim_sum[16];
int cov_n;
function automatic void note_lim(int lim, longint t);
  lim_n[lim]++;
  lim_sum[lim] += t;
endfunction

checker chk_edge(logic sig, logic clk);
  int cnt = 0;
  always_ff @(posedge clk) if (sig) cnt <= cnt + 1;
  a1: assert property (@(posedge clk) sig |-> ##1 !sig || cnt > 0);
  function automatic int get(); return cnt; endfunction
endchecker
checker chk_pair(logic a, b, clk);
  default clocking @(posedge clk); endclocking
  a1: assert property (a |=> b) else lim_n[15]++;
  c1: cover property (a ##1 b) cov_n++;
endchecker
checker chk_val(logic [3:0] v, logic clk, int lim = 10);
  default clocking @(posedge clk); endclocking
  a_lim: assert property (v < lim) else note_lim(lim, $time);
endchecker

module c17a;
  // 17 checkers
  logic clk_g = 0, a = 0, b = 0, c = 0;
  logic [3:0] val = 0;
  int errors = 0;
  always #5 clk_g = ~clk_g;
  chk_edge e1(a, clk_g);
  chk_pair p1(a, b, clk_g);
  chk_val v1(val, clk_g, 5);
  chk_val v2(val, clk_g, 3);
  chk_val v3(val, clk_g);
  initial begin
    @(negedge clk_g); a = 1; b = 1; val = 2;
    @(negedge clk_g); a = 0; b = 1; val = 4;
    @(negedge clk_g); a = 1; b = 1; val = 6;
    @(negedge clk_g); a = 0; b = 1; val = 1;
    repeat (3) @(negedge clk_g);
    if (e1.cnt !== 2) begin $error("e1.cnt = %0d, expected 2", e1.cnt); errors++; end
    if (e1.get() !== 2) begin $error("e1.get() = %0d, expected 2", e1.get()); errors++; end
    if (lim_n[5] !== 1 || lim_sum[5] !== 35) begin
      $error("lim 5: %0d failures, time sum %0d, expected 1 and 35", lim_n[5], lim_sum[5]); errors++; end
    if (lim_n[3] !== 2 || lim_sum[3] !== 60) begin
      $error("lim 3: %0d failures, time sum %0d, expected 2 and 60", lim_n[3], lim_sum[3]); errors++; end
    if (lim_n[10] !== 0) begin $error("lim 10: %0d failures, expected 0", lim_n[10]); errors++; end
    if (lim_n[15] !== 0) begin $error("a |=> b failed %0d times, expected 0", lim_n[15]); errors++; end
    if (cov_n !== 2) begin $error("cover a ##1 b hit %0d times, expected 2", cov_n); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
