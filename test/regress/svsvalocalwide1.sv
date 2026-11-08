// Expected values: IEEE 1800-2017 16.8 (local variables, sequence match items,
// including variables wider than 64 bits), 16.9.3 (sampled values), 16.12.6
// (implication), 11.4 / 11.5 (operators on 100..200-bit vectors, 4-state), 16.14.
// Reference model: a procedural process records the sampled value of d at each
// of the 40 clock edges (k = 0..39, time 5+10k, cyc sampled at edge k is k).
// Attempts that complete by edge 39 are counted:
//   - implication with antecedent constant 1: |-> completes at edge k (40
//     attempts), |=> at edge k+1 (39 attempts); all tautologies -> 0 failures
//   - p_fail fails exactly when d changes between consecutive edges
//   - p_seq / p_eq_const are bare sequences, which fail for every attempt
//     that cannot match; p_eq_const's first element holds only for cyc==3.
// Pass counts for properties whose antecedent can be false are checked as a
// range, because whether a vacuous success runs the pass action is not relied on.
module svsvalocalwide1;
  logic clk = 0;
  logic [99:0] d = 100'h0;
  logic [199:0] e = 200'h0;
  logic signed [127:0] sd = 128'sh0;
  logic [63:0] n64 = 64'h0;
  logic [7:0] sh = 0;
  logic [99:0] dx;
  int cyc = 0;
  int p[string], f[string];
  always #5 clk = ~clk;
  always @(posedge clk) begin
    cyc <= cyc + 1;
    d  <= (d << 3) + 100'h1_2345_6789_abcd_ef01_2345 ^ {36'h0, n64};
    e  <= {e[135:0], d[63:0]} + 200'd1;
    sd <= sd - 128'sh0123_4567_89ab_cdef_0123_4567_89ab_cdef;
    n64 <= n64 + 64'h9e3779b97f4a7c15;
    sh <= sh + 5;
    dx <= (cyc == 7) ? 100'bx : d;
  end
  `define CHK(name, prop) \
    a_``name: assert property (@(posedge clk) prop) p[`"name`"]++; else f[`"name`"]++;

  property p_add;
    logic [99:0] x;
    (1, x = d) |=> d == ((x << 3) + 100'h1_2345_6789_abcd_ef01_2345 ^ {36'h0, n64 - 64'h9e3779b97f4a7c15});
  endproperty
  property p_cmp;
    logic [99:0] x;
    (cyc > 0, x = d) |=> (d > x) || (d < x) || (d == x);
  endproperty
  property p_sub;
    logic [199:0] y;
    (1, y = e) |=> (e - y) != 0 || e == y;
  endproperty
  property p_shift;
    logic [99:0] x;
    (1, x = d) |-> ((x >> sh[3:0]) << sh[3:0]) <= x;
  endproperty
  property p_bits;
    logic [199:0] y;
    (1, y = e) |-> (y[199:100] == e[199:100]) && (y[7:0] == e[7:0]) && (&y == &e) && (|y == |e) && (^y == ^e);
  endproperty
  property p_cond;
    logic [127:0] s;
    (1, s = sd) |=> (s < sd ? 1'b1 : s >= sd);
  endproperty
  property p_signed;
    logic signed [127:0] s;
    (1, s = sd) |=> (sd - s) == -128'sh0123_4567_89ab_cdef_0123_4567_89ab_cdef;
  endproperty
  property p_cast;
    logic [199:0] y;
    (1, y = e) |-> 100'(y) == e[99:0];
  endproperty
  property p_neg;
    logic [99:0] x;
    (1, x = d) |-> (x + (-x)) == 100'h0 && (~x ^ x) == {100{1'b1}};
  endproperty
  property p_unk;
    logic [99:0] x;
    (cyc >= 1, x = dx) |-> (x === 100'bx) || (x == d) || (x !== 100'bx);
  endproperty
  property p_unk2;
    logic [99:0] x;
    (1, x = dx) ##1 (1, x = x + 1) |-> 1;
  endproperty
  property p_seq;
    logic [99:0] x;
    @(posedge clk) (1, x = d) ##2 (d != x) ##1 (d - x != 0);
  endproperty
  property p_fail;
    logic [99:0] x;
    (1, x = d) |=> d == x;
  endproperty
  property p_eq_const;
    logic [99:0] x;
    (cyc == 3, x = d) ##3 (d == x + 100'd7 + (d - x - 100'd7));
  endproperty

  `CHK(add, p_add)
  `CHK(cmp, p_cmp)
  `CHK(sub, p_sub)
  `CHK(shift, p_shift)
  `CHK(bits, p_bits)
  `CHK(cond, p_cond)
  `CHK(signed, p_signed)
  `CHK(cast, p_cast)
  `CHK(neg, p_neg)
  `CHK(unk, p_unk)
  `CHK(unk2, p_unk2)
  `CHK(fail, p_fail)
  a_seq: assert property (p_seq) p["seq"]++; else f["seq"]++;
  a_eqc: assert property (@(posedge clk) p_eq_const) p["eqc"]++; else f["eqc"]++;

  initial begin
    automatic string names[] = '{"add","cmp","sub","shift","bits","cond","signed","cast","neg","unk","unk2","fail","seq","eqc"};
    foreach (names[i]) begin p[names[i]] = 0; f[names[i]] = 0; end
  end
  // ---- reference model ----
  logic [99:0] D[40];
  int mk;
  always @(posedge clk) if (mk < 40) begin D[mk] = d; mk++; end
  int errs;
  function automatic int gp(string n); return p.exists(n) ? p[n] : 0; endfunction
  function automatic int gf(string n); return f.exists(n) ? f[n] : 0; endfunction
  task automatic expect_cnt(string n, int ep_lo, int ep_hi, int ef);
    if (gf(n) != ef) begin $error("%s: %0d failures, expected %0d", n, gf(n), ef); errs++; end
    if (gp(n) < ep_lo || gp(n) > ep_hi) begin $error("%s: %0d passes, expected %0d..%0d", n, gp(n), ep_lo, ep_hi); errs++; end
  endtask
  initial begin
    int ef_fail, ep_fail, ef_seq, ep_seq;
    #399;
    // p_fail: |=> attempts k=0..38
    for (int k = 0; k < 39; k++) if (D[k+1] != D[k]) ef_fail++; else ep_fail++;
    // p_seq: attempt k fails at edge k+2 (first test) or k+3 (second test)
    for (int k = 0; k <= 37; k++) begin
      if (D[k+2] == D[k]) ef_seq++;
      else if (k <= 36) begin
        if (D[k+3] - D[k] == 0) ef_seq++; else ep_seq++;
      end
    end
    expect_cnt("add", 39, 39, 0);
    expect_cnt("sub", 39, 39, 0);
    expect_cnt("cond", 39, 39, 0);
    expect_cnt("signed", 39, 39, 0);
    expect_cnt("shift", 40, 40, 0);
    expect_cnt("bits", 40, 40, 0);
    expect_cnt("cast", 40, 40, 0);
    expect_cnt("neg", 40, 40, 0);
    expect_cnt("cmp", 38, 39, 0);
    expect_cnt("unk", 39, 40, 0);
    expect_cnt("unk2", 39, 39, 0);
    expect_cnt("fail", ep_fail, ep_fail, ef_fail);
    expect_cnt("seq", ep_seq, ep_seq, ef_seq);
    expect_cnt("eqc", 1, 1, 39);
    if (ef_fail == 0 || ep_fail != 0) begin $error("model sanity: d should change every edge"); errs++; end
    if (errs == 0) $display("PASSED");
  end
  initial #400 $finish;
endmodule
