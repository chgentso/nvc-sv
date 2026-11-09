// IEEE 1800-2017 16.12.14 / 16.12.1: the condition of accept_on, reject_on,
// sync_accept_on, sync_reject_on and disable iff shall not refer to local
// variables (including local formal arguments, 16.8.2).  16.10: a local
// variable flows out of "or" only if it is assigned on both operands (or
// before them, e.g. by its declaration assignment); one assigned in both
// operands of "and" / "intersect" does not flow out of them; one assigned in
// an operand of "and" / "intersect" is not visible in the other operand; an
// unassigned local variable shall not be referenced.  16.6: a function called
// in a sequence or property expression has no output or ref arguments.
module svalocal1;
  logic clk, a, b, c;
  logic [3:0] d;
  function automatic bit f(int v); return v > 2; endfunction
  function automatic bit fo(int v, output int o); o = v; return 1'b1; endfunction
  function automatic bit fr(ref int v); return v > 2; endfunction

  property p1; int x; @(posedge clk) (a, x = d) |=> accept_on (x == 1) b; endproperty            // OK
  property p2; int x; @(posedge clk) (a, x = d) |=> reject_on (x == 1) b; endproperty            // OK
  property p3; int x; @(posedge clk) (a, x = d) |=> sync_accept_on (f(x)) b; endproperty         // OK
  property p4; int x; @(posedge clk) (a, x = d) |=> sync_reject_on (x[0]) b; endproperty         // OK
  property p5(local input int y); @(posedge clk) disable iff (y == 0) a |=> b; endproperty        // Error
  property p6; int x; @(posedge clk) disable iff (x == 0) (a, x = d) |=> b; endproperty           // Error
  property p7; int x; @(posedge clk) (a, x = d) |=> accept_on (c) (d == x); endproperty           // OK
  property p8(int y); @(posedge clk) disable iff (y == 0) a |=> b; endproperty                    // OK

  sequence s1; int x; ((a, x = d) or b) ##1 (d == x); endsequence                                 // Error
  sequence s2; int x = 0; ((a, x = d) or b) ##1 (d == x); endsequence                             // OK
  sequence s3; int x; ((a, x = d) or (b, x = 1)) ##1 (d == x); endsequence                        // OK
  sequence s4; int x; ((a, x = d) and (b, x = 1)) ##1 (d == x); endsequence                       // Error
  sequence s5; int x; ((a, x = d) ##1 c intersect b ##1 (c, x = 1)) ##1 (d == x); endsequence     // Error
  sequence s6; int x; (a, x = d) and (b ##1 (d == x)); endsequence                                // Error
  sequence s7; int x = 1; (a, x = d) intersect (b ##0 (d == x)); endsequence                      // OK
  sequence s8; int x, y; ((a, x = d) and (b, y = d)) ##1 (d == x + y); endsequence                // OK
  sequence s9; int x = 1; ((a, x = d) and b) ##1 (d == x); endsequence                            // OK
  sequence s10; int x = 1, y; ((a ##1 (b, x = d)) intersect (c ##1 (1, y = d))) ##1 (x == y); endsequence // OK
  sequence s11; int x; ((a, x = d) or (b ##1 (c, x = d))) ##1 ((a or (b, x = 0)) ##1 (d == x)); endsequence // OK
  sequence s12; int x; a ##1 (d == x); endsequence                                                // Error
  sequence s13; int x, o; (a, x = d) ##1 fo(x, o); endsequence                                    // Error
  sequence s14; int x; (a, x = d) ##1 fr(x); endsequence                                          // Error
  sequence s15; int x; (a, x = d) ##1 (b, x = f(x) ? x + 1 : x) ##1 f(x); endsequence             // OK
endmodule
