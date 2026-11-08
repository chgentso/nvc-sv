// Expected values: IEEE 1800-2017 6.6.1 (wire resolution: equal strong values merge, conflicting values give x,
// z loses to any other driver), 23.3.3.? (inout ports connect the same net, partially for part-select actuals),
// VHDL side (IEEE 1076 std_logic resolution, 'Z' vs value, 'X' on conflict) as
// written in svinout2.vhd. The mapping of std_logic 'X' to Verilog x is assumed (std_logic -> logic).
`timescale 1ns/1ns
module svinout2;
  wire [3:0] b, seen, bs, seen3;
  wire [1:0] sseen;
  reg [3:0] drv = 4'bz;
  reg en = 0;
  integer err = 0;
  assign b = drv;
  l1 u (.b(b), .en(en), .seen(seen));
  l3 u3 (.b(bs), .en(1'b0), .seen(seen3));
  assign bs = drv;
  wire [3:0] bb; reg [3:0] drv2 = 4'bzzzz; assign bb = drv2;
  ls us (.b(bb), .seen(sseen));
  initial begin
    // VHDL drivers off: only the Verilog driver is active
    #1 drv = 4'b0101; #1;
    if (b !== 4'b0101 || seen !== 4'b0101 || seen3 !== 4'b0101) begin
      err = err + 1; $error("sv   b=%b seen=%b seen3=%b", b, seen, seen3);
    end
    // only the VHDL driver (1100) active
    drv = 4'bz; en = 1; #1;
    if (b !== 4'b1100 || seen !== 4'b1100) begin err = err + 1; $error("vhdl b=%b seen=%b", b, seen); end
    // both drive 1100
    drv = 4'b1100; #1;
    if (b !== 4'b1100 || seen !== 4'b1100) begin err = err + 1; $error("same b=%b seen=%b", b, seen); end
    // conflicting bits 3 and 0
    drv = 4'b0101; #1;
    if (b !== 4'bx10x || seen !== 4'bx10x) begin err = err + 1; $error("conf b=%b seen=%b", b, seen); end
    // slice: an inout VHDL port bit that no process drives is a source with its default value 'U'
    // (IEEE 1076 14.7.3.2); 'U' resolved with a Verilog 0 gives x. The VHDL entities drive nothing on bb[3],
    // bb[0] and bb[2] (s2.b(1)), so those bits are x; with bb[2] = x the condition b(1) = '1' is false and
    // s2.b(0) drives 'Z', so bb[1] is just the Verilog 0.
    drv = 4'bz; en = 0; drv2 = 4'b0100; #1;
    if (bb !== 4'bxx0x || sseen !== 2'bx0) begin err = err + 1; $error("slice bb=%b sseen=%b", bb, sseen); end
    // the same undriven-port-bit effect for another Verilog value
    drv2 = 4'b0000; #1;
    if (bb !== 4'bxx0x || sseen !== 2'bx0) begin err = err + 1; $error("slice bb=%b sseen=%b", bb, sseen); end
    if (err == 0) $display("PASSED");
  end
endmodule
