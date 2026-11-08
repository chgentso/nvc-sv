// Package export declarations: explicit, wildcard and *::* exports, and re-import through the exporter
// Expected values: IEEE 1800-2017 26.6 (exporting imported declarations), 26.3 (import), 6.19 (enum cast)
package a;
  typedef int my_t;
  typedef enum { RED, GREEN, BLUE } col_t;
  int cnt = 40;
  function automatic int f(int x); return x + 1; endfunction
  function automatic int g(int x); return x * 2; endfunction
endpackage
package b;
  import a::*;
  export a::my_t, a::f;
  function automatic int h(int x); return f(x) + g(x); endfunction
endpackage
package c;
  import a::*;
  col_t first = RED;
  function automatic int use_g(int x); return g(x); endfunction
  export *::*;
endpackage
package d;
  import a::*;
  my_t y = 1;
  export a::*;
endpackage
module svpkgexport1;
  import b::*;
  my_t v = 7;
  c::col_t k = c::col_t'(1);
  d::my_t w = 3;
  int errs = 0;
  initial begin
    if (v !== 7 || f(4) !== 5 || h(3) !== 10) begin $error("b: %0d %0d %0d", v, f(4), h(3)); errs++; end
    if (k != 1 || c::g(5) !== 10 || c::use_g(2) !== 4) begin $error("c: %0d %0d %0d", k, c::g(5), c::use_g(2)); errs++; end
    if (w !== 3) begin $error("d: %0d", w); errs++; end
    begin
      import c::*;
      automatic col_t q = col_t'(2);
      if (q != 2 || g(6) !== 12) begin $error("c import: %0d %0d", q, g(6)); errs++; end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
