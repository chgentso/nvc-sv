// Self-checking. Expected values: IEEE 1800-2017 16.5.1/16.5.2 (a concurrent assertion is evaluated at its
// clock tick; the antecedent of |=> is evaluated at the tick, the consequent at the next tick), 16.9.1 (##N
// delays count clock ticks), 16.14.1 (pass/fail actions).
// The LRM does not fix how often a function with side effects is evaluated, so only the following is checked:
// every call happens at a clock tick (t = 5 mod 10), and the consequent functions are called at the ticks where
// the attempts that need them are resolved (t=35 and 65 for f(2), t=55 and 85 for f(3)); the pass/fail results
// of all assertions, including one that must fail at t=35 and t=65, are checked.
`timescale 1ns/1ns
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module svasidefx1;
  int errs = 0;
  bit clk = 0, en = 0, x = 0, y = 0;
  time c1[$], c2[$], c3[$];
  time bad4[$];
  int nfail = 0;
  always #5 clk = !clk;
  function bit f(int id);
    if ($time % 10 != 5) begin errs++; $error("f%0d called off-tick at %0t", id, $time); end
    case (id)
      1: c1.push_back($time);
      2: c2.push_back($time);
      3: c3.push_back($time);
    endcase
    return 1;
  endfunction
  function bit fz(); return 0; endfunction
  A1: assert property (@(posedge clk) f(1) |-> x) else nfail++;
  A2: assert property (@(posedge clk) en |=> f(2)) else nfail++;
  A3: assert property (@(posedge clk) en ##1 y |-> ##2 f(3)) else nfail++;
  A4: assert property (@(posedge clk) en |=> fz()) else bad4.push_back($time);
  function automatic bit has(time l[$], time t);
    foreach (l[i]) if (l[i] == t) return 1;
    return 0;
  endfunction
  initial begin
    x = 1;
    #22 en = 1; #10 en = 0; y = 1; #20 en = 1; #10 en = 0; #60;
    `CHK("A1 evaluated", c1.size() > 0, 1'b1)
    `CHK("f2 at 35", has(c2, 35), 1'b1) `CHK("f2 at 65", has(c2, 65), 1'b1)
    `CHK("f3 at 55", has(c3, 55), 1'b1) `CHK("f3 at 85", has(c3, 85), 1'b1)
    `CHK("A1-A3 failures", nfail, 0)
    `CHK("A4 failure count", bad4.size(), 2)
    if (bad4.size() == 2) begin `CHK("A4 fail 1", bad4[0], 35) `CHK("A4 fail 2", bad4[1], 65) end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
