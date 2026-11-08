// Self-checking. Expected values: IEEE 1800-2017 10.9.2 (struct assignment patterns: positional, named,
// default), 11.4.11 (conditional operator with assignment patterns takes its type from the assignment
// context), 10.3 (continuous assignment re-evaluation), 7.2.1 (packed struct {a,b}: a is the upper nibble).
`timescale 1ns/1ns
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0h expected %0h", tag, got, exp); end
package p;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } s_t;
endpackage
module svassignpat1;
  int errs = 0;
  p::s_t t, u, w;
  p::s_t x, y;
  logic en = 0, sel = 0;
  assign t = en ? '{a: 4'h3, b: 4'hc} : '{default: '0};
  assign u = '{4'h1, 4'h2};
  assign w = en ? '{4'h5, 4'h6} : sel ? '{a: 4'h7, b: 4'h8} : '{default: 4'hf};
  initial begin
    #1;
    `CHK("t0", t, 8'h00) `CHK("u0", u, 8'h12) `CHK("w0", w, 8'hff)
    en = 1; #1;
    `CHK("t1", t, 8'h3c) `CHK("u1", u, 8'h12) `CHK("w1", w, 8'h56)
    en = 0; sel = 1; #1;
    `CHK("t2", t, 8'h00) `CHK("u2", u, 8'h12) `CHK("w2", w, 8'h78)
    x = en ? '{4'h1, 4'h2} : '{4'h3, 4'h4};
    y = sel ? '{a: 4'ha, b: 4'hb} : '{default: 0};
    `CHK("x", x, 8'h34) `CHK("y", y, 8'hab)
    if (errs == 0) $display("PASSED");
  end
endmodule
