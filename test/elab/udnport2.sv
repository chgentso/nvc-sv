// IEEE 1800-2017 23.3.3: if only one side of a port is of a user-defined
// nettype the data types shall match and the port shall be an input or
// output
package udnport2_pkg;
  typedef struct { real v; bit valid; } pt_t;
  function automatic real rsum (input real d[]); return 0.0; endfunction
  function automatic pt_t psum (input pt_t d[]); return d[0]; endfunction
  nettype real rnet with rsum;
  nettype pt_t pnet with psum;
endpackage

module udnport2_r import udnport2_pkg::*; (output real o);
endmodule

module udnport2_v import udnport2_pkg::*; (output logic [3:0] o);
endmodule

module udnport2_p import udnport2_pkg::*; (output pt_t o);
endmodule

module udnport2;
  import udnport2_pkg::*;
  rnet r;
  pnet p;
  udnport2_r u1(.o(r));                 // OK
  udnport2_v u2(.o(r));                 // Error
  udnport2_p u3(.o(p));                 // Error (sorry)
endmodule
