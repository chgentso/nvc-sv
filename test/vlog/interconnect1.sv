// IEEE 1800-2017 6.6.8: an interconnect net (or port) has no data type and
// can only appear as a whole or a select in a port connection or another
// interconnect port: not in a continuous or procedural assignment nor in any
// other expression.
module interconnect1_sub (inout interconnect p, inout interconnect [1:0] q);
  assign p = 1'b1;                      // Error
endmodule

module interconnect1;
  interconnect ic;
  interconnect [1:0] ib;
  logic v;
  wire w;
  assign ic = v;                        // Error
  assign w = ic;                        // Error
  initial v = ib[0];                    // Error
  interconnect1_sub u1(.p(ic), .q(ib)); // OK
  interconnect1_sub u2(ib[1], ib);      // OK
  interconnect1_sub u3(.p(ic & v), .q(ib)); // Error
endmodule
