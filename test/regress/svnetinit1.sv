// Net declaration with an initial value that redeclares a port
// Expected values: IEEE 1800-2017 23.2.2.1 (non-ANSI port declarations may be followed by a net declaration of the
// same name), 6.6.2 (triand: wired AND of the drivers), 6.6.3 (tri1: pull-up when undriven), 28.11 (strengths:
// strong0 overrides pull1), 10.3 (continuous assignment).
// I: drivers 1 (weak1) and the pull-up 1 give 1. D: the strong 0 of dd and the pull-up 1 give 0, then 1 when dd is 1.
// o is driven with 1 by its declaration, p = {q, o} with q = a.
module m1(I, D);
  inout I;
  inout D;
  reg ip = 1;
  reg dd = 0;
  tri1 pu;
  triand (weak1, strong0) I = ip;
  triand (weak1, strong0) D = dd;
  assign I = pu;
  assign D = pu;
endmodule
module m2(o, a, p);
  output o;
  input a;
  output [1:0] p;
  wire o = 1'b1;
  wire q = a;
  assign p = {q, o};
endmodule
module svnetinit1;
  wire i, d, o;
  wire [1:0] p;
  reg a = 0;
  int fails = 0;
  m1 u(.I(i), .D(d));
  m2 v(o, a, p);
  initial begin
    #1 if ({i, d, o, p} !== 5'b1_0_1_01) begin $error("t=1: i=%b d=%b o=%b p=%b", i, d, o, p); fails++; end
    u.dd = 1; a = 1;
    #1 if ({i, d, o, p} !== 5'b1_1_1_11) begin $error("t=2: i=%b d=%b o=%b p=%b", i, d, o, p); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
