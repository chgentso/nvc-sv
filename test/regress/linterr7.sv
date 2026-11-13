module linterr7;
  import linterr7_missing_pkg::*;
  logic [1:0] a;
  logic x = 0;
  assign a[IDX0] = x;
endmodule
