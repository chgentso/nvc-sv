// Forward declarations of classes and uses of members before they are known
package p;
  typedef class b;
  typedef class g;

  class a;
    b           other;
    g #(int)    gi;
    g #(string) gs;

    int later_member = 100;

    function int get_other();
      // Member of a class that is declared later
      return other.val + later_member;
    endfunction

    function g #(int) get_gi();
      return gi;
    endfunction

  endclass

  class b;
    int val = 5;
    a   back;
    extern function int twice();
  endclass

  function int b::twice();
    return val * 2 + back.later_member;
  endfunction

  class g #(type T = int, type U = T);
    T item;
    U copy;
    static int count = 0;
    extern function void set(T v);
    extern task later(U v);
    extern static function int inc();
    extern function new();
  endclass

  function g::new();
    count++;
  endfunction

  function void g::set(T v);
    item = v;
    copy = v;
  endfunction

  task g::later(U v);
    #1 copy = v;
  endtask

  function int g::inc();
    return ++count;
  endfunction

  class c #(int W = 4, int X = W * 2);
    function int get();
      return X;
    endfunction
  endclass

  typedef g #(byte) alias_later;
endpackage

module svfwd1;
  import p::*;

  `define check(x, y) \
    if ((x) !== (y)) begin $display("FAILED %s", `"x`"); failed++; end

  int failed = 0;

  initial begin
    automatic a h = new;
    automatic g #(int, int) gi2;
    automatic c ci = new;
    automatic c #(3) c3 = new;
    automatic c #(3, 1) c31 = new;
    automatic alias_later al = new;

    h.other = new;
    h.other.back = h;
    h.gi = new;
    h.gs = new;

    `check(h.get_other(), 105);
    `check(h.other.twice(), 110);

    h.gi.set(7);
    `check(h.get_gi().item, 7);
    `check(h.gi.copy, 7);
    h.gs.set("x");
    `check(h.gs.item, "x");

    h.gi.later(8);
    `check(h.gi.copy, 8);

    // Static members of the specialisation
    `check(g #(int)::count, 1);
    `check(g #(int)::inc(), 2);
    `check(g #(string)::inc(), 2);

    // Same specialisation written in a different way
    gi2 = h.gi;
    `check(gi2.item, 7);

    `check(ci.get(), 8);
    `check(c3.get(), 6);
    `check(c31.get(), 1);

    al.set(8'h12);
    `check(al.item, 8'h12);

    if (failed == 0) $display("PASSED");
    $finish;
  end
endmodule
