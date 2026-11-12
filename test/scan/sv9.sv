module leak_b;
`ifdef LEAKED_MACRO
  import pkg_leaked::*;
`endif
endmodule
