// Edge-sensitive processes in sub-modules whose input ports are driven from constants at time 0
// Expected values: IEEE 1800-2017 9.4.2 (z/x to 0 is a negedge, z/x to 1 is a posedge), 4.7, 10.3.
// Whether an always already waits when the port net first takes its value at time 0 is not specified
// (nondeterministic), so after time 0 only the bits that cannot see an edge are checked exactly, and the
// racy bits are only required to be 0 or 1. Later edges, separated by #1, give exact expectations.
`timescale 1ns/1ns
module subp(input c, output reg r);
  initial r = 0;
  always @(posedge c) r <= 1;
endmodule

module subn(input c, output reg r);
  initial r = 0;
  always @(negedge c) r <= 1;
endmodule

module suba(input c, output reg r);
  initial r = 0;
  always @(c) r <= 1;
endmodule

module svportinit2;
  reg c0 = 0, c1 = 1;
  bit c2;
  wire [8:0] r;
  int errs = 0;
  subp p0(c0, r[0]);
  subp p1(c1, r[1]);
  subn n0(c0, r[2]);
  subn n1(c1, r[3]);
  suba a0(c0, r[4]);
  suba a1(c1, r[5]);
  subp p2(c2, r[6]);
  subn n2(c2, r[7]);
  subn n3(c0 | c1, r[8]);
  initial begin
    #1;
    // bits 0,3,6,8 never see a posedge/negedge of their port: they stay 0
    if (r[0] !== 1'b0 || r[3] !== 1'b0 || r[6] !== 1'b0 || r[8] !== 1'b0) begin
      $error("time 1: edge-free bits %b", r); errs++;
    end
    if ((^{r[1], r[2], r[4], r[5], r[7]}) === 1'bx) begin $error("time 1: racy bits not 0/1: %b", r); errs++; end
    #1 c0 = 1;   // posedge for p0, a0 changes; n3 input stays 1
    #1 c1 = 0;   // negedge for n1, a1 changes; n3 input stays 1
    #1 c2 = 1;   // posedge for p2
    #1;
    if (r[0] !== 1'b1 || r[3] !== 1'b1 || r[4] !== 1'b1 || r[5] !== 1'b1 || r[6] !== 1'b1 || r[8] !== 1'b0) begin
      $error("final: %b", r); errs++;
    end
    if ((^{r[1], r[2], r[7]}) === 1'bx) begin $error("final: racy bits not 0/1: %b", r); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
