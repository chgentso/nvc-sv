// Expected values: IEEE 1800-2017 26.3 (package import), 8.25 (parameterized classes: static method
// called through a specialization), 8.24 (out-of-block `extern` method), 6.19 (enum values:
// X=0, Y=1), 8.20 (virtual methods).
package a;
  class ew #(type T = int);
    static function string s(T v);
      return "x";
    endfunction
  endclass
endpackage

package b;
  import a::*;
  typedef enum { X, Y } mode_e;
  class cfg;
    mode_e m = Y;
    function string str();
      return ew#(mode_e)::s(m);
    endfunction
    virtual function void do_print(int p);
      string t = ew#(mode_e)::s(m);
      if (p > 1) $display("%s", ew#(int)::s(p));
    endfunction
    virtual function void do_copy(cfg o);
      m = o.m;
      void'(ew#(mode_e)::s(m));
    endfunction
    extern function new();
  endclass
  function cfg::new();
  endfunction
  class cov #(type C = cfg);
    C c;
    function new(); c = new; endfunction
    function string str(); return c.str(); endfunction
  endclass
endpackage

module svgenericpkg1;
  import b::*;
  int bad = 0;
  initial begin
    automatic cov#(cfg) k = new;
    if (k.str() != "x") begin $error("str=%s", k.str()); bad++; end
    if (int'(k.c.m) != 1) begin $error("m=%0d, want 1", int'(k.c.m)); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
