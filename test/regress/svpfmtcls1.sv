// %p / %0p applied to class handles: null handle, base/derived objects,
// nested handle, self-referencing list (cycle), static members, parameterised
// class, via $sformatf.
// Expected values: IEEE 1800-2017 21.2.1.7 (%p prints the value in the form of
// an assignment pattern: property values in declaration order, integral
// values in decimal, enum values by name, strings and reals by value), 8.4
// (null handle), 8.5.
// Class-handle layout (member-name prefixes, order of inherited vs. own
// properties, static properties, how a cycle or a nested handle is shown, how
// null is spelled) is implementation-defined and is not checked; only the
// presence and relative order of the instance property values, and that
// printing a cyclic structure terminates, are.
typedef enum {RED, GREEN} col_t;
class Base;
  int a = 3;
  static int cnt = 5;
  string s = "hi";
endclass
class Der extends Base;
  real r = 1.5;
  col_t c = GREEN;
  Base nested;
  bit [7:0] b = 8'hAB;
  int q[$] = {1, 2};
endclass
class Node;
  int v;
  Node next;
  function new(int v); this.v = v; endfunction
endclass
class Mid;
  int k = 1;
  static int sk = 9;
  int j = 2;
endclass
class P #(type T = int);
  T val;
endclass
module svpfmtcls1;
  Base b0, b1;
  Der d;
  Node n1, n2;
  Mid m;
  P#(int) pi;
  int errors = 0;
  function automatic bit has_ordered(string txt, string toks[$]);
    int pos = 0, found;
    foreach (toks[i]) begin
      found = -1;
      for (int k = pos; k + toks[i].len() <= txt.len(); k++)
        if (txt.substr(k, k + toks[i].len() - 1) == toks[i]) begin found = k; break; end
      if (found < 0) return 0;
      pos = found + toks[i].len();
    end
    return 1;
  endfunction
  function automatic void ordered(string what, string txt, string toks[$]);
    if (!has_ordered(txt, toks)) begin
      $error("%s: tokens not found in order in '%s'", what, txt);
      errors++;
    end
  endfunction
  string t;
  initial begin
    t = $sformatf("%p", b0);
    if (t.len() == 0) begin $error("%%p of null handle is empty"); errors++; end
    t = $sformatf("%0p", b0);
    if (t.len() == 0) begin $error("%%0p of null handle is empty"); errors++; end
    b1 = new;
    t = $sformatf("%p", b1);   ordered("%p b1", t, '{"3", "hi"});
    t = $sformatf("%0p", b1);  ordered("%0p b1", t, '{"3", "hi"});
    d = new;
    t = $sformatf("%p", d);
    ordered("%p d", t, '{"3", "hi", "1.5", "GREEN", "171", "1", "2"});
    b0 = d;
    t = $sformatf("%p", b0);   ordered("%p b0 -> d", t, '{"3", "hi"});
    d.nested = b1;
    t = $sformatf("%p", d);
    ordered("%p d nested", t, '{"3", "hi", "1.5", "GREEN", "171", "1", "2"});
    t = $sformatf("%0p", d);
    ordered("%0p d nested", t, '{"3", "hi", "1.5", "GREEN", "171", "1", "2"});
    n1 = new(1); n2 = new(2);
    n1.next = n2; n2.next = n1;
    t = $sformatf("%p", n1);   ordered("%p n1", t, '{"1"});   // terminates
    t = $sformatf("%p", n2);   ordered("%p n2", t, '{"2"});
    m = new;
    t = $sformatf("%p", m);    ordered("%p m", t, '{"1", "2"});
    pi = new;
    t = $sformatf("%p", pi);   ordered("%p pi", t, '{"0"});
    t = $sformatf("%p %p", b1, m);
    ordered("%p b1 m", t, '{"3", "hi", "1", "2"});
    t = $sformatf("%p", d);
    ordered("sformatf d", t, '{"3", "hi", "1.5", "GREEN"});
    if (errors == 0) $display("PASSED");
  end
endmodule
