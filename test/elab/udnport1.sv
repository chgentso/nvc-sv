// IEEE 1800-2017 23.3.3.5: a port that connects two nets of user-defined
// nettypes merges them, so the nettypes shall match (a nettype declared as
// another name for an existing one matches it, 6.6.7); a net of a
// user-defined nettype cannot be merged with a net of a built-in net type.
// 6.6.8 / 23.3.3.4: the ports joined by an interconnect net shall have
// compatible net types.
package udnport1_pkg;
  function automatic real res_r (input real d[]); return 0.0; endfunction
  function automatic real res_s (input real d[]); return 1.0; endfunction
  nettype real n1 with res_r;
  nettype real n2 with res_s;
  nettype n1 n1_alias;
endpackage

module udnport1_a import udnport1_pkg::*; (inout n1 p);
endmodule

module udnport1_b import udnport1_pkg::*; (inout n2 p);
endmodule

module udnport1;
  import udnport1_pkg::*;
  n1 x;
  n2 y;
  n1_alias z;
  wire logic w;
  interconnect ic, ic2;
  udnport1_a u1(.p(x));                 // OK
  udnport1_a u2(.p(y));                 // Error
  udnport1_b u3(.p(x));                 // Error
  udnport1_a u4(.p(z));                 // OK
  udnport1_a u5(.p(w));                 // Error
  udnport1_a u6(.p(ic));                // OK
  udnport1_a u7(.p(ic));                // OK
  udnport1_a u8(.p(ic2));               // OK
  udnport1_b u9(.p(ic2));               // Error
endmodule
