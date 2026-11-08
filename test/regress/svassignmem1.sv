// Self-checking. Expected values: IEEE 1800-2017 10.3.2 (continuous assignment to a member of a struct
// variable: each member may have its own driver), 6.5 (variables driven by one continuous assignment),
// 10.3 (re-evaluation when the right-hand side changes).
`timescale 1ns/1ns
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
typedef struct { logic [7:0] x; logic [7:0] y; } d_t;
module svassignmem1;
  int errs = 0;
  d_t v, w;
  logic [7:0] a = 1;
  assign v.x = a + 1;
  assign w.y = v.x;
  initial begin
    #1;
    `CHK("v.x", v.x, 2) `CHK("w.y", w.y, 2)
    a = 5;
    #1;
    `CHK("v.x'", v.x, 6) `CHK("w.y'", w.y, 6)
    if (errs == 0) $display("PASSED");
  end
endmodule
