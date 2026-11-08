// Package enum constants in continuous assignments, parameters and concatenations
// (sensitivity to the interface variable that is compared with an enum constant).
// Expected values: IEEE 1800-2017 6.19 (enum constants have the value of the base type),
// 10.3 (continuous assignment is re-evaluated when an operand changes), 11.4.11
// (conditional operator), 11.4.12 (concatenation), 23.2.? / 25 (interface variables),
// 6.5 / 6.6 (z for an undriven net), 6.20 (parameter of enum type).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
package pk;
  typedef enum logic [1:0] {Host = 2'd1, Device = 2'd2} mode_e;
endpackage
interface ifc(input logic clk);
  pk::mode_e mode = pk::Host;
  logic [3:0] d_int = 4'h5;
  wire [3:0] d;
  assign d = (mode == pk::Host) ? d_int : 'z;
endinterface
module sub #(parameter pk::mode_e M = pk::Device)(output logic [1:0] o);
  assign o = M;
endmodule
module svenumsens1;
  typedef enum logic {A, B} e1_t;
  logic clk = 0;
  ifc i(clk);
  wire [1:0] w;
  logic [3:0] cat;
  sub s(.o(w));
  assign cat = {pk::Host, pk::Device};
  e1_t e = B;
  wire [1:0] w2 = {B, A};
  int err = 0;
  initial begin
    #1;
    `CHK(i.d === 4'h5, "d while mode == Host")
    `CHK(w === 2'b10, "parameter of enum type")
    `CHK(cat === 4'b0110, "concatenation of package enum constants")
    `CHK(w2 === 2'b10, "concatenation of local enum constants")
    i.mode = pk::Device;
    #1;
    `CHK(i.d === 4'bzzzz, "d re-evaluated after mode changes")
    if (err == 0) $display("PASSED");
  end
endmodule
