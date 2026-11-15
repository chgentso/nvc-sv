module linterr15(input logic clk);
  int broken = ;
  logic x;
  always @(posedge clk) x <= 1;
  always @* x = 0;
endmodule
