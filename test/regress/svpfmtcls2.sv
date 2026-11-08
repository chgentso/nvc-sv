// %p / %0p applied to class handles with many kinds of properties: static,
// local, protected, rand, fixed/dynamic arrays, associative array, structs,
// 4-state and signed/unsigned integers, real, event, strings, handles to other
// objects (including a self-reference through a queue).
// Expected values: IEEE 1800-2017 21.2.1.7 (%p prints the property values in
// declaration order as an assignment pattern: integral values in decimal,
// reals by value), clause 8 (classes).  The ordering of inherited vs. own
// properties, the member-name prefixes, whether static/local/protected members
// and events are shown, the spelling of x/z digits and of nested handles are
// implementation-defined and are not checked: only the presence and relative
// order of the instance property values of the base class A, and the
// termination of printing a cyclic handle graph, are.
typedef struct { int x; byte y; } st_t;
typedef struct packed { bit [3:0] hi; bit [3:0] lo; } pk_t;
class A;
  static int s0 = 1;
  local int priv = 2;
  protected int prot = 3;
  rand int rv = 4;
  int arr[3] = '{7, 8, 9};
  int dyn[];
  int aa[string];
  st_t st = '{5, 6};
  pk_t pk = 8'hA5;
  string e;
  event ev;
  logic [3:0] l4 = 4'bx01z;
  time t = 10;
  shortint si = -3;
  int unsigned u = 4000000000;
  real rr = 2.25;
  function new(); dyn = new[2]; aa["k"] = 1; endfunction
endclass
class B extends A;
  A h[2];
  A hq[$];
  A ha[string];
  static int s1 = 11;
endclass
module svpfmtcls2;
  A a1, a2;
  B b1;
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
  task automatic ordered(string what, string txt);
    // arr, st, si, u, rr of class A
    if (!has_ordered(txt, '{"7", "8", "9", "5", "6", "-3", "4000000000", "2.25"})) begin
      $error("%s: property values not found in order in '%s'", what, txt);
      errors++;
    end
  endtask
  string t;
  initial begin
    a1 = new; a2 = new; b1 = new;
    b1.h[0] = a2; b1.h[1] = null; b1.hq.push_back(a1); b1.hq.push_back(b1); b1.ha["z"] = a2;
    t = $sformatf("%p", a1);   ordered("%p a1", t);
    t = $sformatf("%0p", a1);  ordered("%0p a1", t);
    t = $sformatf("%p", b1);   ordered("%p b1", t);   // terminates despite b1 in b1.hq
    t = $sformatf("%0p", b1);  ordered("%0p b1", t);
    if (errors == 0) $display("PASSED");
  end
endmodule
