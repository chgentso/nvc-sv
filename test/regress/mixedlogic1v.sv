module mixedlogic1v_leaf (output logic o);
  assign o = 1'b1;
endmodule

module mixedlogic1v_m (input logic clk, output logic o, output logic [3:0] v,
                      output logic p, output logic q, inout logic s,
                      inout logic t);
  logic r;
  assign o = 1'b1;
  assign v = 4'b1010;
  assign s = 1'bz;
  assign t = 1'b0;
  mixedlogic1v_leaf u (.o(r));
  assign q = r;
  initial p = 1'b1;
endmodule
