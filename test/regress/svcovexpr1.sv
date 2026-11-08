// Coverpoints on expressions: enum casts, arithmetic, concatenation, negation
// Expected values: IEEE 1800-2017 19.5 (coverpoint expressions, bins[]), 19.5.7 (bin values are
// evaluated in the type of the expression; an enum's base type applies, so value 0 gives a
// bin of sl_e'(...) although it is not an enumerator), 11.6.1 (expression widths),
// 19.8.1 (sample arguments), 19.11 (coverage = covered/total bins; group = average of the
// coverpoints, weight 1)
// Bins: cw 2; cs B[0..2] + C = 4; cx lo, hi = 2; cy 16; ci neg, pos = 2.
package p;
  typedef enum bit {READ=0, WRITE=1} tx_e;
  typedef enum bit [15:0] {S0=1, S1=2, S2=4} sl_e;
  class tx;
    bit [3:0] a;
    bit [3:0] b;
    tx_e w;
    sl_e s;
    int i;
  endclass
  class cov;
    covergroup cg with function sample(tx t);
      option.per_instance = 1;
      cw: coverpoint tx_e'(t.w) { bins T[] = {0, 1}; }
      cs: coverpoint sl_e'(t.s) { bins B[] = {[0:2]}; bins C = {4}; }
      cx: coverpoint t.a + t.b { bins lo = {[0:7]}; bins hi = {[8:15]}; }
      cy: coverpoint {t.a[1:0], t.b[1:0]} { bins v[] = {[0:15]}; }
      ci: coverpoint -t.i { bins neg = {[-3:-1]}; bins pos = {[1:3]}; }
    endgroup
    function new();
      cg = new;
    endfunction
  endclass
endpackage

module svcovexpr1;
  import p::*;
  int errs = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  initial begin
    automatic cov c = new;
    automatic tx t = new;
    chkr("0", c.cg.get_coverage(), 0.0);
    // sample 1: cw T[1]; cs B[2]; cx 5 -> lo; cy {3,2} = 14; ci -2 -> neg
    t.a = 3; t.b = 2; t.w = WRITE; t.s = S1; t.i = 2;
    c.cg.sample(t);
    chkr("1", c.cg.get_coverage(), (50.0 + 25.0 + 50.0 + 6.25 + 50.0) / 5);
    // sample 2: cw T[0]; cs C; cx: 9+7 = 16 never hits hi (it wraps to 0 in 4 bits, or hits no bin);
    // cy {1,3} = 7; ci +1 -> pos
    t.a = 9; t.b = 7; t.w = READ; t.s = S2; t.i = -1;
    c.cg.sample(t);
    chkr("2", c.cg.get_coverage(), (100.0 + 50.0 + 50.0 + 12.5 + 100.0) / 5);
    chkr("cw", c.cg.cw.get_coverage(), 100.0);
    chkr("cs", c.cg.cs.get_coverage(), 50.0);
    chkr("cx", c.cg.cx.get_coverage(), 50.0);
    chkr("cy", c.cg.cy.get_coverage(), 12.5);
    chkr("ci", c.cg.ci.get_coverage(), 100.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
