`include "defs.svh"

package my_pkg;
  typedef logic [`WIDTH-1:0] byte_t;
endpackage

interface bus_if (input logic clk);
  logic [7:0] data;
  modport mst (output data);
endinterface

module leaf (input logic clk, output logic [7:0] q);
endmodule

module top (input logic clk, bus_if.mst bus);
  import other_pkg::*;
  import my_pkg::byte_t;
  my_pkg::byte_t v;
  logic [7:0] w;
  virtual vif_if vi;
  leaf u0 (.clk(clk), .q(w));
  missing_mod u1 (.clk(clk));
  assign v = ext_pkg::K;
endmodule

program tb_prog;
endprogram

primitive inv (output o, input i);
  table 0 : 1; 1 : 0; endtable
endprimitive
