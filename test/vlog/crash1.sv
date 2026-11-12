class cfg #(parameter int C);
  localparam int W = C + 1;
endclass

module top;
  cfg #(MISSING + 1) c1;
  cfg #(MISSING) c2;
endmodule
