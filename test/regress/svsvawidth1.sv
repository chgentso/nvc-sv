// Expected values: IEEE 1800-2017 16.9.3 (the sampled value functions: $past and $sampled
// return the type of their argument, $rose, $fell, $stable and $changed a single bit) with
// 11.6 and 11.8 (the operands of an expression are context-determined: 8 bits here, so
// 8'hff + 1 wraps to 0 and a one-bit result in a concatenation is one bit wide).
`timescale 1ns/1ns
module svsvawidth1;
  logic clk = 0;
  logic [7:0] d = 8'hff;
  logic [7:0] r = 0;
  logic g = 0;
  int errs;
  always #5 clk = ~clk;
  always @(posedge clk) r <= d + 8'd1;

  // The sum wraps at the width of the operands
  a1: assert property (@(posedge clk) 1 |=> r == $past(d) + 8'd1) else begin errs++; $display("a1 failed"); end
  a2: assert property (@(posedge clk) 1 |=> r == 8'($past(d) + 8'd1)) else begin errs++; $display("a2 failed"); end
  a3: assert property (@(posedge clk) 1 |=> r == 8'($past(d)) + 8'd1) else begin errs++; $display("a3 failed"); end
  a4: assert property (@(posedge clk) $sampled(d) + 8'd1 == 8'd0) else begin errs++; $display("a4 failed"); end
  // With a wider operand it does not
  a5: assert property (@(posedge clk) 1 |=> 9'($past(d)) + 9'd1 == 9'h100) else begin errs++; $display("a5 failed"); end
  // The comparison is done in 9 bits when the other operand has 9
  a6: assert property (@(posedge clk) 1 |=> $past(d) + 8'd1 == 9'h100) else begin errs++; $display("a6 failed"); end

  // One bit each: the concatenation is {1, 0} when d is steady
  a7: assert property (@(posedge clk) 1 |=> {$stable(d), $changed(d)} == 2'b10) else begin errs++; $display("a7 failed"); end
  a8: assert property (@(posedge clk) 1 |=> {$rose(g), $fell(g)} == 2'b00) else begin errs++; $display("a8 failed"); end
  a9: assert property (@(posedge clk) 1 |=> ({7'b0, $stable(d)} + 8'd255) == 8'd0) else begin errs++; $display("a9 failed"); end

  initial begin
    #52;
    if (errs != 0) $fatal(1, "%0d assertions failed", errs);
    $display("PASSED");
    $finish;
  end
endmodule
