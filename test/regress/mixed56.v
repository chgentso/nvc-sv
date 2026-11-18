// Outputs and inouts of the Verilog module left out of the VHDL component
// are unassociated as in a default binding (IEEE 1076-2008 7.3.3)
module mixed56_v (input a, input b, output o1, output o2, inout io,
                  output [3:0] v);
  assign o1 = a & b;
  assign o2 = ~a;
  assign v = 4'd9;
endmodule
