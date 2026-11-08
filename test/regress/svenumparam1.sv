// Enum base types sized by package parameters.
// Expected values: IEEE 1800-2017 6.19 (enum values and names; the base type gives the
// width), 6.19.5.6 (name()), 20.6.2 ($bits of an enum type is the width of its base type),
// 6.20.2 (localparam / parameter expressions).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
package svenumparam1_pk;
  parameter int W = 4;
  localparam int V = W + 1;
  typedef enum logic [W-1:0] { A = 4'b0000, B = 4'b0001, C = 4'b1010 } e_t;
  typedef enum bit [V-1:0] { D = 5'd3, E = 5'b11111 } f_t;
endpackage
module svenumparam1;
  import svenumparam1_pk::*;
  e_t x;
  f_t y;
  int err = 0;
  initial begin
    x = B; y = E;
    `CHK(x == 1 && x.name() == "B", "B")
    `CHK(y == 31 && y.name() == "E", "E")
    x = C;
    `CHK(x == 10 && x.name() == "C", "C")
    `CHK($bits(e_t) == 4, "$bits(e_t)")
    `CHK($bits(f_t) == 5, "$bits(f_t)")
    if (err == 0) $display("PASSED");
  end
endmodule
