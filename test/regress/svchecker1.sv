// Expected values: IEEE 1800-2017 17 (checkers: instantiation in a module, formal arguments,
// assertions inside a checker, checker variables, always_ff), 16.9 / 16.12 ($stable, |=>, |->,
// ##1 evaluated on values sampled in the Preponed region), 16.14 (assert action blocks,
// cover property pass statement).  Reference model: values of v/d/en sampled at each clock edge.
`timescale 1ns/1ns
checker chk_hold(logic clk, logic v, logic [7:0] d);
  a_hold: assert property (@(posedge clk) v |=> $stable(d))
    else svchecker1.note_hold($time);
endchecker : chk_hold

checker chk_cnt(logic clk, logic en);
  logic prev = 0;
  always_ff @(posedge clk) prev <= en;
  c_two: cover property (@(posedge clk) en ##1 en) svchecker1.note_cov($time);
  c_n: assert property (@(posedge clk) prev |-> en) else svchecker1.note_drop($time);
endchecker

module svchecker1;
  logic clk = 0, v = 0, en = 0;
  logic [7:0] d = 0;
  int errors = 0;
  time got_hold[$], got_drop[$], got_cov[$];   // from the checkers
  time exp_hold[$], exp_drop[$], exp_cov[$];   // from the reference model
  function void note_hold(time t); got_hold.push_back(t); endfunction
  function void note_drop(time t); got_drop.push_back(t); endfunction
  function void note_cov(time t);  got_cov.push_back(t);  endfunction
  always #5 clk = ~clk;
  chk_hold c0 (clk, v, d);
  chk_cnt c1 (clk, en);

  // Reference model, one step per clock edge, on the values sampled before the edge
  // (stimulus changes at 12, 22, 32, never at an edge, so the live values are the sampled ones).
  bit pv = 0, pen = 0;
  logic [7:0] pd = 0;
  always @(posedge clk) begin
    if (pv && d !== pd) exp_hold.push_back($time);     // v |=> $stable(d)
    if (pen && !en) exp_drop.push_back($time);         // prev |-> en, prev = en sampled at the previous edge
    if (pen && en) exp_cov.push_back($time);           // en ##1 en: attempt began at the previous edge, matches now
    pv = v; pd = d; pen = en;
  end

  initial begin
    #12 v = 1; d = 1; en = 1; #10 d = 2; #10 v = 0; en = 0;
    #20;
    // Expected from the traces: v=1 sampled at 15 and 25, d: 1 (15), 2 (25), 2 (35)
    //   -> hold violated at 25 only; en 1,1,0 at 15,25,35 -> en dropped at 35;
    //   en ##1 en matches for the attempt started at 15 (en at 15 and 25), reported at 25.
    if (exp_hold.size() != 1 || exp_drop.size() != 1 || exp_cov.size() != 1)
      begin $error("reference model is degenerate: %0d %0d %0d", exp_hold.size(), exp_drop.size(), exp_cov.size()); errors++; end
    if (got_hold != exp_hold) begin $error("hold violations %p, expected %p", got_hold, exp_hold); errors++; end
    if (got_drop != exp_drop) begin $error("en drops %p, expected %p", got_drop, exp_drop); errors++; end
    if (got_cov != exp_cov) begin $error("cover hits %p, expected %p", got_cov, exp_cov); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
