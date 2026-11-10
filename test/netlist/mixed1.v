module vsub #(parameter W = 4) (input wire clk, input wire [W-1:0] d,
                                output reg [W-1:0] q, inout wire io);
  wire [W-1:0] t;
  assign t = d + 1;
  always @(posedge clk) q <= t;
endmodule
