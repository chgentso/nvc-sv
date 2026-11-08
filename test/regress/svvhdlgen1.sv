// Expected values: IEEE 1800-2017 9.4.1 (#delay), 23.6 (hierarchical names into a foreign-language instance);
// IEEE 1076 for the VHDL body (generate g(i) counts 1+i per rising clk edge, 3-bit wrap).
// clk rises at 5, 15, 25 before the check at 32: c0 = 3*1 = 3, c1 = 3*2 = 6.
`timescale 1ns/1ns
module svvhdlgen1;
  logic clk = 0;
  always #5 clk = ~clk;
  svvhdlgen1_v u (.clk(clk));
  wire [2:0] c0 = u.g[0].cnt;
  wire [2:0] c1 = u.g[1].cnt;
  initial begin
    #32;
    if (c0 !== 3'd3 || c1 !== 3'd6) $error("c0=%0d c1=%0d, expected 3 and 6", c0, c1);
    else $display("PASSED");
    $finish;
  end
endmodule
