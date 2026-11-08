// A net driven more than once in a process phase and read through
// VHDL conversions in nested instances
module mixed28_leaf (input i, output o);
    assign o = ~i;
endmodule

module mixed28_mid (input [3:0] a, output [3:0] y);
    mixed28_inner #(.W(4)) u (a, y);
endmodule
