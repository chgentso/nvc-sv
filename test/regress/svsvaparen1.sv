// Expected values: IEEE 1800-2017 16.9.1/16.8 (parenthesised sequence and property
// instances are the same sequence or property), 16.10 (local variable x assigned from a
// sampled a at the first tick and compared at the next tick), 16.12 (disable iff),
// 16.14.1 (pass action on every success, including vacuous success: c4), 16.14.3 (cover:
// one hit per non-vacuous successful attempt), 16.5.1 (sampled values), 18.13 ($urandom_range).
// The random v is not predicted; a reference model evaluates the sampled history of v, w, a.
`timescale 1ns/1ns
module svsvaparen1;
  logic clk = 0, v = 0, w = 0;
  logic [3:0] a = 0;
  int c1, c2, c3, c4;
  always #5 clk = ~clk;
  sequence s_vw;
    v ##1 w;
  endsequence
  property p_vw;
    v |-> ##1 w;
  endproperty
  sequence s_loc;
    bit [3:0] x;
    (v, x = a) ##1 w && x == a;
  endsequence
  if (1) begin : g
    C1: cover property (@(posedge clk) (s_vw)) c1++;
    C2: cover property (@(posedge clk) disable iff (a == 9) (s_loc)) c2++;
  end
  C3: cover property (@(posedge clk) ((s_vw))) c3++;
  A4: assert property (@(posedge clk) (p_vw)) c4++;
  int sv[$], sw[$], sa[$];
  always @(posedge clk) begin sv.push_back(v === 1'b1); sw.push_back(w === 1'b1); sa.push_back(a); end
  int errs;
  task automatic cmp(string nm, int got, int exp);
    if (got !== exp) begin $error("%s: got %0d expected %0d", nm, got, exp); errs++; end
  endtask
  task automatic check();
    int n = sv.size(), e1 = 0, e2 = 0, e4 = 0;
    for (int k = 0; k < n; k++) begin
      // C1/C3: v ##1 w ; A4: v |-> ##1 w (vacuous successes count for the pass action)
      if (!sv[k]) e4++;
      else if (k + 1 < n && sw[k+1]) begin e1++; e4++; end
      // C2: (v, x = a) ##1 w && x == a, disabled while a == 9 (a is not sampled for the disable)
      if (sv[k] && k + 1 < n && sw[k+1] && sa[k] == sa[k+1] && sa[k] != 9 && sa[k+1] != 9) e2++;
    end
    cmp("c1", c1, e1); cmp("c2", c2, e2); cmp("c3", c3, e1); cmp("c4", c4, e4);
    if (n < 14) begin $error("too few ticks %0d", n); errs++; end
  endtask
  initial begin
    w = 1;
    repeat (12) begin
      @(negedge clk);
      v = $urandom_range(0, 1); a = a + 1;
    end
    @(negedge clk); v = 0;
    #20;
    check();
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
