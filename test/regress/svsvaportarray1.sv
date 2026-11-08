// Expected values: IEEE 1800-2017 23.2.2 (unpacked array ports), 16.12.6 (|->), 16.12 (disable
// iff), 16.14.1 (fail action), 16.14.3 (cover property counts non-vacuous successes),
// 16.5.1 (preponed sampling: data_o == data_i[idx_o] holds in every sampled tick because dout
// is the continuous function of d and idx), 10.3 (continuous assignment).
// A reference model of the sampled rst/vld history predicts the number of non-vacuous
// enabled attempts; the assertion must never fail.
`timescale 1ns/1ns
module arb(input clk_i, input rst_ni, input logic [7:0] data_i [4], input logic [1:0] idx_o, input logic ready_i, input logic valid_o, input logic [7:0] data_o);
  int nfail, nhit;
  assert property (@(posedge clk_i) disable iff (!rst_ni) ready_i && valid_o |-> data_o == data_i[idx_o])
    else nfail++;
  cover property (@(posedge clk_i) disable iff (!rst_ni) ready_i && valid_o |-> data_o == data_i[idx_o]) nhit++;
endmodule
module svsvaportarray1;
  logic clk = 0, rst = 0;
  logic [7:0] d [4];
  logic [1:0] idx;
  logic [7:0] dout;
  logic vld = 0;
  arb u(.clk_i(clk), .rst_ni(rst), .data_i(d), .idx_o(idx), .ready_i(1'b1), .valid_o(vld), .data_o(dout));
  always #5 clk = ~clk;
  assign dout = d[idx];
  always @(posedge clk) begin
    idx <= idx + 1;
    d[idx] <= d[idx] + 8'd3;
    vld <= ~vld;
  end
  bit sr[$], sv[$];
  always @(posedge clk) begin sr.push_back(rst === 1'b1); sv.push_back(vld === 1'b1); end
  initial begin
    for (int i = 0; i < 4; i++) d[i] = i * 8'd16;
    idx = 0;
    #12 rst = 1;
    #100;
    begin
      automatic int n = sr.size(), exp = 0, errs = 0;
      for (int k = 0; k < n; k++) if (sr[k] && sv[k]) exp++;
      if (n != 11) begin $error("ticks %0d", n); errs++; end
      if (u.nfail != 0) begin $error("assertion failed %0d times", u.nfail); errs++; end
      if (u.nhit != exp) begin $error("non-vacuous successes %0d expected %0d", u.nhit, exp); errs++; end
      if (exp == 0) begin $error("model sanity"); errs++; end
      if (errs == 0) $display("PASSED");
    end
    $finish;
  end
endmodule
