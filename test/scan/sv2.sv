module cond;
`ifdef USE_A
  import pkg_a::*;
`else
  import pkg_b::*;
`endif
endmodule
