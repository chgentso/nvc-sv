// Verilog parameters set from VHDL generics of vector, BOOLEAN and
// enumeration types
module mixed36_sub #(parameter vec = 8'h00, parameter bv = 4'h0,
                     parameter flag = 0, parameter mode = 0)
   (input clk, output [7:0] o, output [3:0] p, output q, output [1:0] r);
   assign o = vec;
   assign p = bv;
   assign q = flag;
   assign r = mode;
endmodule
