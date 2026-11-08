// Expected values: IEEE 1800-2017 8.27 (forward typedef of a class), 8.10 (static methods),
// 6.20.6 (const), 10.5/6.21 (package variable initialisers), 7.5/10.9 (array assignment pattern).
package p;
  typedef class cfg;
  const cfg DEFAULTS[] = '{
    cfg::make("a", 1, 2),
    cfg::make("b", 3, 4)
  };
  const cfg ONE = cfg::make("one", 7, 8);
  class cfg;
    string name; int lo, hi;
    static function cfg make(string name, int lo, int hi);
      cfg c = new();
      c.name = name; c.lo = lo; c.hi = hi;
      return c;
    endfunction
  endclass
endpackage
module svfwdclassinit1;
  import p::*;
  int bad = 0;
  initial begin
    if (DEFAULTS.size() != 2) begin $error("size %0d", DEFAULTS.size()); bad++; end
    else begin
      if (DEFAULTS[0].name != "a" || DEFAULTS[0].lo != 1 || DEFAULTS[0].hi != 2) begin $error("DEFAULTS[0]"); bad++; end
      if (DEFAULTS[1].name != "b" || DEFAULTS[1].lo != 3 || DEFAULTS[1].hi != 4) begin $error("DEFAULTS[1]"); bad++; end
    end
    if (ONE == null) begin $error("ONE is null"); bad++; end
    else begin
      if (ONE.name != "one" || ONE.lo != 7 || ONE.hi != 8) begin $error("ONE"); bad++; end
    end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
    $finish;
  end
endmodule
