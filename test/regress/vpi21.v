// Verilog part of test vpi21

module vpi21_sub #(parameter w = 4) (input wire clk,
                                     input wire [w-1:0] d,
                                     output reg [w-1:0] q);
   wire [w-1:0] t;
   reg [w-1:0]  r;
   reg          z;
   assign t = d + 1;
   always @(posedge clk) q <= t;
   always @(posedge clk) r[1:0] <= t[1:0];
   initial z = 1'b0;
endmodule
