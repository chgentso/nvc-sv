// A tri0 or tri1 net is pulled when nothing drives it
// Expected values: IEEE 1800-2017 6.6.2 (tri0/tri1 nets), 28.9 (pullup), 28.12 (strength resolution:
// a strong 0 overrides the pull-up strength), 23.3.3 (port connections)
`timescale 1ns/1ns
module svtripull1_sub(inout a);
  assign a = 1'bz;
endmodule
module svtripull1;
  tri1 n1;
  tri1 n2;
  tri0 n3;
  wire n4;
  pullup(n4);
  tri1 n5;
  reg en = 0;
  int errs;
  assign n1 = 1'bz;
  svtripull1_sub s2(n2);
  assign n5 = en ? 1'b0 : 1'bz;
  initial begin
    #1;
    if (n1 !== 1'b1 || n2 !== 1'b1 || n3 !== 1'b0 || n4 !== 1'b1 || n5 !== 1'b1) begin $error("undriven: %b %b %b %b %b", n1, n2, n3, n4, n5); errs++; end
    en = 1; #1;
    if (n5 !== 1'b0) begin $error("driven 0: %b", n5); errs++; end
    en = 0; #1;
    if (n5 !== 1'b1) begin $error("released: %b", n5); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
