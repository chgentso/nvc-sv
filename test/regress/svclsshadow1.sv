// Property hiding in derived classes: the static type of a handle selects the member,
// super reaches the base copy, `new h` copies all properties, randomize calls post_randomize
// Expected values: IEEE 1800-2017 8.15 (super), 8.14 (overriding members; a derived
// declaration hides the base one), 8.12 (shallow copy), 18.4/18.5 (constraints bind to
// the property of their own scope), 18.6.1 and 18.6.2 (randomize, post_randomize)
module svclsshadow1;
  int errs = 0;
  function automatic void chks(string m, string got, string exp);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", m, got, exp); errs++; end
  endfunction
  class b0;
    string label = "b0";
    int v = 1;
    rand bit [3:0] r;
    constraint c0 { r == 3; }
    function string s0(); return $sformatf("%s %0d %0d", label, v, r); endfunction
  endclass
  class b1 extends b0;
    string label;
    int v = 2;
    rand bit [7:0] r;
    constraint c1 { r == 107; }
    function string s1();
      return $sformatf("%s %0d %0d %s %0d %0d", label, v, r, super.label, super.v, super.r);
    endfunction
  endclass
  class b2 extends b1;
    rand int x;
    int posted = 0;
    string post_label;
    constraint c2 { x == r + 1; }
    function string s2(); return $sformatf("%s %0d %0d %0d", label, v, r, x); endfunction
    function void post_randomize(); posted++; post_label = label; endfunction
  endclass
  initial begin
    automatic b2 h = new, k;
    b0 p0;
    b1 p1;
    h.label = "set";
    h.v = 5;
    chks("outside", $sformatf("%s %0d", h.label, h.v), "set 5");
    chks("h.s0", h.s0(), "b0 1 0");
    chks("h.s1", h.s1(), "set 5 0 b0 1 0");
    chks("h.s2", h.s2(), "set 5 0 0");
    p1 = h;
    p0 = h;
    chks("via b1", $sformatf("%s %0d", p1.label, p1.v), "set 5");
    chks("via b0", $sformatf("%s %0d", p0.label, p0.v), "b0 1");
    p0.label = "viab0";
    p1.v = 9;
    chks("h.s0 b", h.s0(), "viab0 1 0");
    chks("h.s1 b", h.s1(), "set 9 0 viab0 1 0");
    chks("h.s2 b", h.s2(), "set 9 0 0");
    if (!h.randomize()) begin $error("randomize failed"); errs++; end
    if (h.posted != 1) begin $error("post_randomize ran %0d times", h.posted); errs++; end
    chks("post label", h.post_label, "set");
    chks("h.s1 c", h.s1(), "set 9 107 viab0 1 3");
    chks("h.s2 c", h.s2(), "set 9 107 108");
    chks("h.s0 c", h.s0(), "viab0 1 3");
    k = new h;
    chks("k.s1", k.s1(), "set 9 107 viab0 1 3");
    chks("k.s2", k.s2(), "set 9 107 108");
    k.label = "k";
    chks("h.s1 d", h.s1(), "set 9 107 viab0 1 3");
    chks("k.s1 d", k.s1(), "k 9 107 viab0 1 3");
    if (errs == 0) $display("PASSED");
  end
endmodule
