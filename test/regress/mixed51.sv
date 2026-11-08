`timescale 1ns/1ns
// Hierarchical reads of VHDL generics from Verilog: a generic of the VHDL
// top-level entity, and generics of a nested VHDL instance whose generic map
// overrides one default.
// Expected values: IEEE 1800-2017 23.6 (hierarchical names), 11.4 (operators),
// 12.7.1 (for loop).  mixed51.vhd gives top_gen = -3 (default), s.width = 9 (from
// the generic map), s.depth = 2 (default).  Sum of 0..8 = 36; 9 * -3 = -27.
module mixed51;
  mixed51_e u();
  integer i, n;
  int errors = 0;
  initial begin
    #1;
    if (u.top_gen != -3 || u.s.width != 9 || u.s.depth != 2) begin
      $error("a: %0d %0d %0d, expected -3 9 2", u.top_gen, u.s.width, u.s.depth); errors++; end
    n = 0;
    for (i = 0; i < u.s.width; i = i + 1) n = n + i;
    if (n != 36 || u.s.width * u.top_gen != -27) begin
      $error("b: %0d %0d, expected 36 -27", n, u.s.width * u.top_gen); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
