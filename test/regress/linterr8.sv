module linterr8(input logic clk, input logic [7:0] d);
  int broken = ;
  logic [7:0] a8;
  logic [3:0] b4;
  logic [3:0] w4;
  always @* if (b4 == 0) a8 = 1;
  assign w4 = d;
endmodule
