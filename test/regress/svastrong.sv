// Strong/weak eventuality and until properties with a vacuous antecedent (self-checking).
// Expected values: IEEE 1800-2017 16.12.1 (property implication; a false antecedent succeeds vacuously),
// 16.12.11/16.12.12/16.12.13 (until, s_until, eventually), 16.12.2 (weak/strong), 16.14.1 (assert: the pass
// action runs on success including vacuous success, the fail action on failure), 20.12.
// Reference model: clk rises at 5,15,25,... ; a is 1 only at the tick at 15, b is always 0.
//  - every attempt started at a tick with a=0 succeeds vacuously at that tick: all six properties "pass" at
//    5,25,35,45,55 (one pass each)
//  - the attempt started at 15 can never succeed (b never holds): p5 and p6 fail at 25 (a drops before b),
//    p2 fails at 45 (window [1:3] closes: ticks 25,35,45), p4 (weak) never fails, p1/p3 (strong) stay
//    unresolved until the end of simulation; how an unresolved strong attempt is reported at the end of
//    simulation is not specified and is not checked.
//  - no pass at 15 (nothing resolves successfully); no failure at any other tick before the end.
// The order of messages inside one time step is not checked.
`timescale 1ns/1ns
module svastrong;
  logic clk = 0, a = 0, b = 0;
  int errs = 0;
  int npass [1:6][0:5];   // [property][tick index (t-5)/10]
  int nfail [1:6][0:6];   // ticks 5..55 and index 6 for anything later/other
  int other_pass = 0;
  always #5 clk = ~clk;
  initial begin
    #12 a = 1;
    #10 a = 0;
    #40 $finish;
  end

  function automatic void rec(int p, bit ok);
    longint t = $time;
    int idx = (t >= 5 && ((t - 5) % 10) == 0 && t <= 55) ? int'((t - 5) / 10) : -1;
    if (ok) begin
      if (idx >= 0) npass[p][idx]++; else other_pass++;
    end else begin
      if (idx >= 0) nfail[p][idx]++; else nfail[p][6]++;   // end-of-run reports land here
    end
  endfunction

  p1: assert property (@(posedge clk) a |-> s_eventually b) rec(1, 1); else rec(1, 0);
  p2: assert property (@(posedge clk) a |-> eventually [1:3] b) rec(2, 1); else rec(2, 0);
  p3: assert property (@(posedge clk) a |-> strong(##[1:$] b)) rec(3, 1); else rec(3, 0);
  p4: assert property (@(posedge clk) a |-> ##[1:$] b) rec(4, 1); else rec(4, 0);
  p5: assert property (@(posedge clk) a |-> (a s_until b)) rec(5, 1); else rec(5, 0);
  p6: assert property (@(posedge clk) a |-> (a until b)) rec(6, 1); else rec(6, 0);

  final begin
    for (int p = 1; p <= 6; p++) begin
      for (int i = 0; i < 6; i++) begin
        automatic int ep = (i == 1) ? 0 : 1;   // tick index 1 is t=15 (a=1): no pass
        automatic int ef = 0;
        if (i == 2 && (p == 5 || p == 6)) ef = 1;   // t=25
        if (i == 4 && p == 2) ef = 1;               // t=45
        if (npass[p][i] != ep) begin errs++; $error("p%0d pass count at t=%0d: %0d, expected %0d", p, 5 + 10*i, npass[p][i], ep); end
        if (nfail[p][i] != ef) begin errs++; $error("p%0d fail count at t=%0d: %0d, expected %0d", p, 5 + 10*i, nfail[p][i], ef); end
      end
      if (p == 4 || p == 5 || p == 6 || p == 2) if (nfail[p][6] != 0) begin errs++; $error("p%0d unexpected late failure", p); end
      if (nfail[p][6] > 1) begin errs++; $error("p%0d more than one end-of-run failure", p); end
    end
    if (other_pass != 0) begin errs++; $error("pass reported at a non-tick time or after the last tick: %0d", other_pass); end
    if (errs == 0) $display("PASSED");
  end
endmodule
