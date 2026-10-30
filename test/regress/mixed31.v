module mixed31_v #(parameter W = 4, parameter K = 1) (
   input        clk,
   input  [7:0] d,
   output [7:0] q );

   assign q = d + K;
endmodule
