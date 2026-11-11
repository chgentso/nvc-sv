// Verilog part of test vpi23

module vpi23_leaf (input wire [3:0] a);
   wire [3:0] n;
   assign n = ~a;
endmodule

module vpi23_sub (input wire clk,
                  input wire [3:0] d,
                  output reg [3:0] q);
   wire [3:0] t;
   reg        z;
   assign t = d + 1;
   always @(posedge clk) q <= t;
   initial z = 1'b0;
   vpi23_leaf u_leaf (.a(t));
endmodule
