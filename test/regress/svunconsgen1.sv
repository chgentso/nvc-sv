// Expected values: IEEE 1800-2017 9.4.1 (#delay, always), 23.2.2 / 23.3 (port connections, parameter overrides on a
// foreign-language instance); IEEE 1076 for the LFSR body (r <= r xor taps on each rising clk_i edge, q_o = r).
// clk rises at 5, 15, ...; after the first edge q = init xor taps, after the second q = init again.
`timescale 1ns/1ns
module svunconsgen1;
  logic clk = 0;
  wire [15:0] q;
  wire [7:0] q8;
  int errs = 0;
  always #5 clk = ~clk;
  svunconsgen1_lfsr #(.g_length(16), .g_taps(16'hb400), .g_init_value(16'hace1)) dut (.clk_i(clk), .q_o(q));
  svunconsgen1_lfsr #(.g_length(8), .g_taps(8'h1d), .g_init_value(8'h81)) dut8 (.clk_i(clk), .q_o(q8));
  initial begin
    #12;
    if (q !== 16'h18e1 || q8 !== 8'h9c) begin errs++; $error("t=12: q=%h q8=%h, expected 18e1 9c", q, q8); end
    #10;
    if (q !== 16'hace1 || q8 !== 8'h81) begin errs++; $error("t=22: q=%h q8=%h, expected ace1 81", q, q8); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
