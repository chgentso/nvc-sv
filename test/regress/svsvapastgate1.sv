// Expected values: IEEE 1800-2017 16.9.3 ($past with number_of_ticks, gating expression and
// clocking event: the value of the expression sampled at the n-th previous clock tick at
// which the sampled gating expression is true), 16.9.2 ($rose, $stable on sampled values),
// 16.14.1 (pass action of a boolean property), 16.5.1 (preponed sampling), 27.5 (generate).
// A reference model over the values of d and g sampled at each posedge predicts, for each of
// the 16 constant k, at how many ticks each $past comparison holds, and the $rose/$stable counts.
// Ticks without enough history return the initial value, which is left unspecified here
// (only an allowance of at most one extra hit for the value 0 is made for those ticks).
`timescale 1ns/1ns
module svsvapastgate1;
  logic clk = 0;
  int d = 0;
  logic g = 0;
  int r5, r6;
  int h1[16], h2[16], h3[16], h4[16];
  always #5 clk = ~clk;
  always @(negedge clk) begin
    d <= d + 1;
    g <= (d % 3) != 1 && d != 6;
  end
  for (genvar k = 0; k < 16; k++) begin : chk
    assert property (@(posedge clk) $past(d, 2, g, @(posedge clk)) == k) h1[k]++; else ;
    assert property (@(posedge clk) $past(d, 1, g) == k) h2[k]++; else ;
    assert property (@(posedge clk) $past(d,,g) == k) h3[k]++; else ;
    assert property (@(posedge clk) $past(d, 2,, @(posedge clk)) == k) h4[k]++; else ;
  end
  assert property (@(posedge clk) $rose(g, @(posedge clk))) r5++; else ;
  assert property (@(posedge clk) $stable(g, @(posedge clk))) r6++; else ;

  int sd[$], sg[$];
  always @(posedge clk) begin sd.push_back(d); sg.push_back(g === 1'b1); end
  initial #150 begin check(); $finish; end

  int errs;
  task automatic check();
    int n = sd.size();
    int e1[16], e2[16], e4[16];
    int u1 = 0, u2 = 0, u4 = 0, er5 = 0, er6 = 0, u6 = 0;
    for (int m = 0; m < n; m++) begin
      int last[$];
      for (int j = 0; j < m; j++) if (sg[j]) last.push_back(j);
      if (last.size() >= 2) e1[sd[last[last.size()-2]]]++; else u1++;
      if (last.size() >= 1) e2[sd[last[last.size()-1]]]++; else u2++;
      if (m >= 2) e4[sd[m-2]]++; else u4++;
      if (m >= 1) begin
        if (sg[m] && !sg[m-1]) er5++;
        if (sg[m] == sg[m-1]) er6++;
      end else u6++;
    end
    if (n != 15) begin $error("ticks %0d", n); errs++; end
    for (int k = 0; k < 16; k++) begin
      int a1 = (k == 0) ? u1 : 0, a2 = (k == 0) ? u2 : 0, a4 = (k == 0) ? u4 : 0;
      if (h1[k] < e1[k] || h1[k] > e1[k] + a1) begin $error("past(d,2,g) k=%0d: %0d expected %0d (+%0d)", k, h1[k], e1[k], a1); errs++; end
      if (h2[k] < e2[k] || h2[k] > e2[k] + a2) begin $error("past(d,1,g) k=%0d: %0d expected %0d (+%0d)", k, h2[k], e2[k], a2); errs++; end
      if (h3[k] < e2[k] || h3[k] > e2[k] + a2) begin $error("past(d,,g) k=%0d: %0d expected %0d (+%0d)", k, h3[k], e2[k], a2); errs++; end
      if (h4[k] < e4[k] || h4[k] > e4[k] + a4) begin $error("past(d,2,,clk) k=%0d: %0d expected %0d (+%0d)", k, h4[k], e4[k], a4); errs++; end
    end
    if (r5 != er5) begin $error("rose: %0d expected %0d", r5, er5); errs++; end
    if (r6 < er6 || r6 > er6 + u6) begin $error("stable: %0d expected %0d (+%0d)", r6, er6, u6); errs++; end
    if (er5 == 0 || er6 == 0) begin $error("model sanity"); errs++; end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
