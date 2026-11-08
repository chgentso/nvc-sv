// Expected values: IEEE 1800-2017 16.7 (sequence cycle delay ##N: N shall be a
// constant_expression; "a + 1" with a a variable is illegal) -> the design
// must be rejected at elaboration/analysis time (testlist flag "fail").
module svsvadelaybound1;
  logic clk = 0, a = 1, b = 1;
  always #5 clk = ~clk;
  assert property (@(posedge clk) a ##(a + 1) b);
  initial #20 $finish;
endmodule
