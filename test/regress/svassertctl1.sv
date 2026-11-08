// Self-checking. Expected values: IEEE 1800-2017 20.12 ($assertoff/$asserton with levels 0 = this assertion
// only; assertions in other modules are unaffected), 16.5.1 (values sampled before the tick), 16.14.1 (the else
// action runs when an attempt fails; attempts that start while the assertion is off do not run actions).
// sub.clk: posedges at 5,15,...; negedges at 10,20,... ; u.a is 1 until t=12, 0 until t=62, 1 until t=87, then 0.
// A1 (posedge, top) is off from t=0 to t=42 and from t=77 on, on in between: it fails at 45 and 55 (a==0), passes
// at 65 and 75. S1 (negedge, in sub) is never disabled: it fails at 20,30,40,50,60 and 90,100.
`timescale 1ns/1ns
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module sub;
  logic clk = 0, a = 1;
  time s1f[$];
  always #5 clk = ~clk;
  S1: assert property (@(negedge clk) a) else s1f.push_back($time);
endmodule
module svassertctl1;
  int errs = 0;
  time a1f[$];
  sub u();
  A1: assert property (@(posedge u.clk) u.a) else a1f.push_back($time);
  initial begin
    $assertoff(0, A1);
    #12 u.a = 0;
    #30 $asserton(0, A1);
    #20 u.a = 1;
    #15 $assertoff(0, A1);
    #10 u.a = 0;
    #20;
    `CHK("A1 failures", a1f.size(), 2)
    if (a1f.size() == 2) begin `CHK("A1 fail 1", a1f[0], 45) `CHK("A1 fail 2", a1f[1], 55) end
    `CHK("S1 failures", u.s1f.size(), 7)
    if (u.s1f.size() == 7) begin
      `CHK("S1 f0", u.s1f[0], 20) `CHK("S1 f1", u.s1f[1], 30) `CHK("S1 f2", u.s1f[2], 40) `CHK("S1 f3", u.s1f[3], 50)
      `CHK("S1 f4", u.s1f[4], 60) `CHK("S1 f5", u.s1f[5], 90) `CHK("S1 f6", u.s1f[6], 100)
    end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
