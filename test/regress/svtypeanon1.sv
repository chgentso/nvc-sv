// Expected values: IEEE 1800-2017 20.6.1 ($typename), 6.19 / 7.2 / 7.3
// (anonymous enum, struct, union types).  The LRM fixes that the result is a
// string describing the resolved type, that the type of a member of a basic
// type prints as that type ("int"), and that objects of one type print alike.
// The exact layout of the text (spacing, whether and how the member list is
// spelled out, how an anonymous type of a package or module is qualified) is
// not relied on: the checks only look for the leading aggregate keyword
// (enum, struct, union) and "packed", that two objects declared together have the same
// type string, and that types of different kind print differently.
package pk;
  struct { int a; } pv;
  enum {PA, PB} pe;
endpackage
module svtypeanon1;
  enum { E1, E2 } e1;
  union { int i; bit [31:0] b; } u1;
  struct { int x; } s1, s1b;
  struct packed { bit a; } p1;
  enum { E3, E4 } e2;
  union packed { bit [3:0] a; bit [3:0] b; } u2;
  struct { int x; } s2;
  int errs;
  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++) if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  function automatic bit starts(string s, string pre);
    return s.len() >= pre.len() && s.substr(0, pre.len() - 1) == pre;
  endfunction
  task automatic chk(bit c, string m); if (!c) begin $error("%s", m); errs++; end endtask
  initial begin
    string te1, tu1, ts1, ts1b, tp1, te2, tu2, ts2, tpv, tpe, tx;
    te1 = $typename(e1); tu1 = $typename(u1); ts1 = $typename(s1); ts1b = $typename(s1b);
    tp1 = $typename(p1); te2 = $typename(e2); tu2 = $typename(u2); ts2 = $typename(s2);
    tpv = $typename(pk::pv); tpe = $typename(pk::pe); tx = $typename(s1.x);
    chk(starts(te1, "enum"), {"e1: ", te1});
    chk(starts(tu1, "union"), {"u1: ", tu1});
    chk(starts(ts1, "struct") && !has(ts1, "packed"), {"s1: ", ts1});
    chk(ts1 == ts1b, {"s1 and s1b share one type but print differently: ", ts1, " / ", ts1b});
    chk(starts(tp1, "struct") && has(tp1, "packed"), {"p1: ", tp1});
    chk(starts(te2, "enum"), {"e2: ", te2});
    chk(starts(tu2, "union") && has(tu2, "packed"), {"u2: ", tu2});
    chk(starts(ts2, "struct") && !has(ts2, "packed"), {"s2: ", ts2});
    chk(starts(tpv, "struct"), {"pk::pv: ", tpv});
    chk(starts(tpe, "enum"), {"pk::pe: ", tpe});
    chk(tx == "int", {"s1.x: ", tx});
    chk(te1 != ts1 && ts1 != tu1 && tu1 != tp1 && tp1 != tu2, "types of different kind print the same");
    if (errs == 0) $display("PASSED");
  end
endmodule
