// Net declaration assignments to output ports (non-ANSI), with constant and variable sources
// Expected values: IEEE 1800-2017 6.7 (net declaration assignment), 23.2.2 (port declarations),
// 10.3 (continuous assignment follows its source)
`timescale 1ns/1ns
module sub1(a0, a1);
  output a0, a1;
  wire d0 = 1;
  reg d1;
  wire a0 = d0;
  wire a1 = d1;
  initial begin
    d1 = 0;
    #2 d1 = 1;
  end
endmodule

module sub2(a2, a3);
  output a2;
  output [3:0] a3;
  wire d2 = 1'b1;
  wire [3:0] d3 = 4'ha;
  wire a2 = ~d2;
  wire [3:0] a3 = d3 ^ 4'h3;
endmodule

module svportnetassign1;
  wire o0, o1, o2;
  wire [3:0] o3;
  int errs = 0;
  sub1 u1(o0, o1);
  sub2 u2(o2, o3);
  initial begin
    #1;
    if ({o0, o1, o2, o3} !== 7'b1_0_0_1001) begin $error("t=1: %b %b %b %b", o0, o1, o2, o3); errs++; end
    #2;
    if ({o0, o1, o2, o3} !== 7'b1_1_0_1001) begin $error("t=3: %b %b %b %b", o0, o1, o2, o3); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
