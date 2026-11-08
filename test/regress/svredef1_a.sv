`include "svredef1.svh"
module svredef1_dep(svredef1_ifc.slave i);
  assign i.w = 1'b1;
endmodule

module svredef1_env;
  svredef1_ifc i();
  svredef1_dep d(i);
  initial #1 $display("w=%b", i.w);
endmodule
