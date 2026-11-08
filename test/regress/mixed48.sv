`timescale 1ns/1ns
// Hierarchical reads of VHDL integer signals from Verilog, used as an array
// index, in arithmetic and in a signed comparison.
// Expected values: IEEE 1800-2017 23.6 (hierarchical names), 11.4 (operators),
// 11.8 (signed arithmetic: an integer signal is read as a signed 32-bit value),
// 7.4.6 (array index).  The VHDL side (mixed48.vhd) gives n = 2 (3 from 5 ns),
// neg = -42 (17 from 15 ns), wide = 100000 (-200000 from 15 ns), nat = 7 (9 from
// 15 ns).  The mapping of a VHDL integer to a signed 32-bit Verilog value follows
// the design of the mixed-language support.
module mixed48;
  logic [7:0] mem [0:3];
  int errors = 0;
  mixed48_e u();
  initial begin
    mem[0] = 8'h10; mem[1] = 8'h11; mem[2] = 8'h12; mem[3] = 8'h13;
    #1 if (mem[u.n] !== 8'h12 || u.n != 2) begin $error("a: mem[u.n] = %h, u.n = %0d, expected 12 and 2", mem[u.n], u.n); errors++; end
    #10 if (mem[u.n] !== 8'h13 || u.n != 3) begin $error("b: mem[u.n] = %h, u.n = %0d, expected 13 and 3", mem[u.n], u.n); errors++; end
    if (u.neg != -42 || u.wide != 100000 || u.nat != 7) begin
      $error("c: %0d %0d %0d, expected -42 100000 7", u.neg, u.wide, u.nat); errors++; end
    if (u.n + u.nat != 10 || (u.n > 1) !== 1'b1) begin
      $error("d: n + nat = %0d, (n > 1) = %b, expected 10 and 1", u.n + u.nat, u.n > 1); errors++; end
    if (!(u.neg < 0)) begin $error("e: u.neg = %0d is not negative", u.neg); errors++; end
    #10 if (u.neg != 17 || u.wide != -200000 || u.nat != 9) begin
      $error("f: %0d %0d %0d, expected 17 -200000 9", u.neg, u.wide, u.nat); errors++; end
    if (u.wide >= 0) begin $error("f: u.wide = %0d is not negative", u.wide); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
