module linterr13(input logic clk);
  logic x, y;
  always @(posedge clk) begin
    y <= 1;
  always @(posedge clk) x <= 1;
  always @* x = 0;
endmodule
