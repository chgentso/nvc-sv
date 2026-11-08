// Expected values: IEEE 1800-2017 16.9.2 (##[m:n], ##[m:$]), 16.12.6 (|->, |=>), 16.14.1
// (pass action on any success including vacuous success, fail action on failure),
// 16.14.3 (cover property: one hit per non-vacuous successful attempt), 16.5.1/16.9.3
// (values sampled in the preponed region; an x operand is not true), 11.4.9 (!x is x).
// A reference model evaluates every attempt on the history of sampled a, b, c with
// three-valued results (pass / fail / not yet decided at the end of the run) and predicts
// all fail, pass and cover counters.
`timescale 1ns/1ns
module svsvaleaf1;
  bit clk;
  logic a, b, c;
  logic [15:0] lfsr = 16'hace1;
  int f1, f2, f3, f4, f5, f6, f7, f8, p1, p2, p3, cv1, cv2, cv3, cv4, cv5;
  initial forever #5 clk = ~clk;
  always @(posedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    a <= lfsr[0] & lfsr[1] & lfsr[4];
    b <= lfsr[3] & lfsr[7];
    c <= lfsr[9] | lfsr[11];
  end
  assert property (@(posedge clk) a |-> b) else f1++;
  assert property (@(posedge clk) a |=> b) else f2++;
  assert property (@(posedge clk) a |-> ##2 b) else f3++;
  assert property (@(posedge clk) a |-> ##[0:3] b) begin p1++; end else f4++;
  assert property (@(posedge clk) a |=> ##[1:4] b) begin p2++; end else f5++;
  assert property (@(posedge clk) a |-> ##[2:$] b) else f6++;
  cover property (@(posedge clk) a |-> ##[0:16] b) cv1++;
  cover property (@(posedge clk) !c |-> ##3 (a && !c)) cv2++;
  cover property (@(posedge clk) a ##[1:3] b ##1 c) cv3++;
  assert property (@(posedge clk) a |-> ##[1:3] b ##[1:3] c) begin p3++; end else f7++;
  assert property (@(posedge clk) c |=> ##[0:2] a ##[2:4] b ##1 c) else f8++;
  cover property (@(posedge clk) !b |-> ##[1:6] a ##[1:6] c) cv4++;
  cover property (@(posedge clk) a ##[1:4] b ##[1:4] c ##[1:4] a) cv5++;
  // ---- reference model ----
  int sa1[$], sb1[$], sb0[$], sc1[$], sc0[$];   // sampled values: ==1 / ==0 flags
  always @(posedge clk) begin   // the LFSR block uses NBAs, so this reads the preponed values
    sa1.push_back(a === 1'b1); sb1.push_back(b === 1'b1); sb0.push_back(b === 1'b0);
    sc1.push_back(c === 1'b1); sc0.push_back(c === 1'b0);
  end
  int n, errs;
  // tri-state element: 1 true, 0 false, 2 unknown (beyond the end of the run)
  function automatic int e(ref int v[$], input int i);
    if (i >= n) return 2;
    return v[i];
  endfunction
  function automatic int and3(int x, int y);
    if (x == 0 || y == 0) return 0;
    if (x == 2 || y == 2) return 2;
    return 1;
  endfunction
  function automatic int or3(int x, int y);
    if (x == 1 || y == 1) return 1;
    if (x == 2 || y == 2) return 2;
    return 0;
  endfunction
  function automatic int impl(int ant, int cons);   // ant is known: vacuous success when false
    return ant ? cons : 1;
  endfunction
  int xf[1:8], xp[1:3], xcv[1:5];
  task automatic model();
    n = sa1.size();
    for (int k = 0; k < n; k++) begin
      int r, c;
      int ak = sa1[k];
      // f1  a |-> b
      r = impl(ak, e(sb1, k)); if (r == 0) xf[1]++;
      // f2  a |=> b
      r = impl(ak, e(sb1, k + 1)); if (r == 0) xf[2]++;
      // f3  a |-> ##2 b
      r = impl(ak, e(sb1, k + 2)); if (r == 0) xf[3]++;
      // f4  a |-> ##[0:3] b
      c = 0; for (int j = 0; j <= 3; j++) c = or3(c, e(sb1, k + j));
      r = impl(ak, c); if (r == 0) xf[4]++; if (r == 1) xp[1]++;
      // f5  a |=> ##[1:4] b
      c = 0; for (int j = 1; j <= 4; j++) c = or3(c, e(sb1, k + 1 + j));
      r = impl(ak, c); if (r == 0) xf[5]++; if (r == 1) xp[2]++;
      // f6  a |-> ##[2:$] b : never fails
      c = 0; for (int j = 2; j <= n - k; j++) c = or3(c, e(sb1, k + j));
      r = impl(ak, c); if (r == 0) xf[6]++;
      // cv1  a |-> ##[0:16] b
      c = 0; for (int j = 0; j <= 16; j++) c = or3(c, e(sb1, k + j));
      if (ak && c == 1) xcv[1]++;
      // cv2  !c |-> ##3 (a && !c)
      if (sc0[k] && and3(e(sa1, k + 3), e(sc0, k + 3)) == 1) xcv[2]++;
      // cv3  a ##[1:3] b ##1 c
      c = 0; for (int j = 1; j <= 3; j++) c = or3(c, and3(e(sb1, k + j), e(sc1, k + j + 1)));
      if (ak && c == 1) xcv[3]++;
      // f7  a |-> ##[1:3] b ##[1:3] c
      c = 0;
      for (int j1 = 1; j1 <= 3; j1++) for (int j2 = 1; j2 <= 3; j2++)
        c = or3(c, and3(e(sb1, k + j1), e(sc1, k + j1 + j2)));
      r = impl(ak, c); if (r == 0) xf[7]++; if (r == 1) xp[3]++;
      // f8  c |=> ##[0:2] a ##[2:4] b ##1 c
      c = 0;
      for (int d1 = 0; d1 <= 2; d1++) for (int d2 = 2; d2 <= 4; d2++)
        c = or3(c, and3(e(sa1, k + 1 + d1), and3(e(sb1, k + 1 + d1 + d2), e(sc1, k + 2 + d1 + d2))));
      r = impl(sc1[k], c); if (r == 0) xf[8]++;
      // cv4  !b |-> ##[1:6] a ##[1:6] c
      c = 0;
      for (int j1 = 1; j1 <= 6; j1++) for (int j2 = 1; j2 <= 6; j2++)
        c = or3(c, and3(e(sa1, k + j1), e(sc1, k + j1 + j2)));
      if (sb0[k] && c == 1) xcv[4]++;
      // cv5  a ##[1:4] b ##[1:4] c ##[1:4] a
      c = 0;
      for (int j1 = 1; j1 <= 4; j1++) for (int j2 = 1; j2 <= 4; j2++) for (int j3 = 1; j3 <= 4; j3++)
        c = or3(c, and3(e(sb1, k + j1), and3(e(sc1, k + j1 + j2), e(sa1, k + j1 + j2 + j3))));
      if (ak && c == 1) xcv[5]++;
    end
  endtask
  task automatic cmp(string nm, int got, int exp);
    if (got !== exp) begin $error("%s: got %0d expected %0d", nm, got, exp); errs++; end
  endtask
  initial begin
    #3000;
    model();
    cmp("ticks", n, 300);
    cmp("f1", f1, xf[1]); cmp("f2", f2, xf[2]); cmp("f3", f3, xf[3]); cmp("f4", f4, xf[4]);
    cmp("f5", f5, xf[5]); cmp("f6", f6, xf[6]); cmp("f7", f7, xf[7]); cmp("f8", f8, xf[8]);
    cmp("p1", p1, xp[1]); cmp("p2", p2, xp[2]); cmp("p3", p3, xp[3]);
    cmp("cv1", cv1, xcv[1]); cmp("cv2", cv2, xcv[2]); cmp("cv3", cv3, xcv[3]);
    cmp("cv4", cv4, xcv[4]); cmp("cv5", cv5, xcv[5]);
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
