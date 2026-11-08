// Verilog module with vector ports instantiated from VHDL as UNSIGNED
// and SIGNED ports
module mixed20_sub(input [3:0] a, input [3:0] b, output [3:0] y,
                   output [3:0] z);
   assign y = a + b;
   assign z = a - b;
endmodule
