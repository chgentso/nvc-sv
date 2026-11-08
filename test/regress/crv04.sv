module crv04;
  class C;
    rand int a;   // := weights
    rand int b;   // :/ weights over range
    rand int z;   // zero weight
    rand int r;   // range with :=
    constraint k { a dist {0 := 1, 1 := 3};
                   b dist {[0:3] :/ 4, 10 :/ 4};
                   z dist {1 := 1, 2 := 0, [5:6] := 0, 3 := 1};
                   r dist {[0:1] := 1, 5 := 2}; }
  endclass
  C o = new();
  int errors = 0;
  int N = 8000;
  int i;
  int ca0, ca1, cb_lo, cb10, cbother, cz1, cz3, czbad, cr01, cr5, crother;
  initial begin
    for (i = 0; i < N; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a == 0) ca0++; else if (o.a == 1) ca1++; else errors++;
      if (o.b >= 0 && o.b <= 3) cb_lo++; else if (o.b == 10) cb10++; else cbother++;
      if (o.z == 1) cz1++; else if (o.z == 3) cz3++; else czbad++;
      if (o.r == 0 || o.r == 1) cr01++; else if (o.r == 5) cr5++; else crother++;
    end
    // a: 1 is 75%
    if (ca1 < N*0.65 || ca1 > N*0.85) begin errors++; $display("a: %0d %0d", ca0, ca1); end
    // b: half in 0..3, half 10
    if (cbother != 0) begin errors++; $display("b other %0d", cbother); end
    if (cb_lo < N*0.4 || cb_lo > N*0.6) begin errors++; $display("b: %0d %0d", cb_lo, cb10); end
    if (czbad != 0) begin errors++; $display("zero weight value seen %0d", czbad); end
    if (cz1 < N*0.4 || cz1 > N*0.6) begin errors++; $display("z: %0d %0d", cz1, cz3); end
    // r: [0:1]:=1 -> 0 and 1 weight 1 each, 5 weight 2 -> 5 is 50%
    if (crother != 0) begin errors++; $display("r other"); end
    if (cr5 < N*0.4 || cr5 > N*0.6) begin errors++; $display("r: %0d %0d", cr01, cr5); end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
