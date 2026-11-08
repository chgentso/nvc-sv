// Expected values: IEEE 1800-2017 16.7 / 16.12 (an operand of sequence
// concatenation ##1 shall be a sequence; "(b |-> c)" is a property, which is
// illegal there) -> the design must be rejected (testlist flag "fail").
module svsvadelayprop1;
  logic clk = 0, a = 1, b = 1, c = 1;
  always #5 clk = ~clk;
  assert property (@(posedge clk) a ##1 (b |-> c));
  initial #20 $finish;
endmodule
