// Expected values: IEEE 1800-2017 8.10 (static methods called through the class scope),
// 8.7/8.9 (new, handles), 13.4 (function calls, void casting), 26.3 (package scope access).
// A method is called on the handle returned by a static function (S::get().bump(3)).
class S;
  int n;
  static S inst;
  static function S get(); if (inst == null) inst = new; return inst; endfunction
  function void bump(int k); n += k; endfunction
  function int count(); return n; endfunction
endclass
package pk;
  class R; static R r; static function R get(); if (r == null) r = new; return r; endfunction int v; function void set(int x); v = x; endfunction function int val(); return v; endfunction endclass
endpackage
module svcallchain1;
  int errors = 0;
  int got;
  initial begin
    S::get().bump(3);
    S::get().bump(4);
    got = S::get().count();
    if (got !== 7) begin $error("count = %0d, expected 7", got); errors++; end
    pk::R::get().set(9);
    got = pk::R::get().val();
    if (got !== 9) begin $error("val = %0d, expected 9", got); errors++; end
    got = pk::R::get().v;
    if (got !== 9) begin $error("v = %0d, expected 9", got); errors++; end
    void'(S::get().count());
    // the singleton is the same object every time
    if (S::get() != S::get()) begin $error("get() returned different objects"); errors++; end
    if (S::get().n !== 7) begin $error("n = %0d, expected 7", S::get().n); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
