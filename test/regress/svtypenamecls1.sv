// Expected values: IEEE 1800-2017 20.6.1 ($typename of class types), 8.25
// (parameterized classes: each distinct set of parameter values is a distinct
// class type, a default specialization is the same type as the explicit one),
// 26 (packages).
// The exact text for a specialization (how the parameter list is rendered,
// scope qualification) is implementation-defined; checked: the class name
// occurs, the parameter values occur (type argument and numeric value),
// different specializations print differently, equal specializations print
// alike.
package pk;
  class P #(type T = int, int N = 1); T x; endclass
  class Q; endclass
endpackage
class U #(int N = 2); endclass
module svtypenamecls1;
  class C #(type T = int, int N = 3); T x; endclass
  C#(real,5) c1; C c2; pk::P#(real, 4) p1; pk::P p2; pk::Q q; U#(7) u1; U u2;
  C#(int,3) c3; pk::P#(int, 1) p3; U#(2) u3; C#(real,6) c4;
  int errs;
  function automatic bit has(string str, string sub);
    for (int i = 0; i + sub.len() <= str.len(); i++) if (str.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  task automatic hs(string got, string sub, string what);
    if (!has(got, sub)) begin $error("%s: \"%s\" does not contain \"%s\"", what, got, sub); errs++; end
  endtask
  task automatic same(string a, string b, string what);
    if (a != b) begin $error("%s: \"%s\" vs \"%s\" should print alike", what, a, b); errs++; end
  endtask
  task automatic differ(string a, string b, string what);
    if (a == b) begin $error("%s: both print \"%s\"", what, a); errs++; end
  endtask
  initial begin
    string s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    s1 = $typename(c1); s2 = $typename(c2); s3 = $typename(p1); s4 = $typename(p2);
    s5 = $typename(q); s6 = $typename(u1); s7 = $typename(u2);
    s8 = $typename(c3); s9 = $typename(p3); s10 = $typename(u3); s11 = $typename(c4);
    hs(s1, "C", "c1"); hs(s1, "real", "c1"); hs(s1, "5", "c1");
    hs(s2, "C", "c2"); hs(s2, "int", "c2"); hs(s2, "3", "c2");
    hs(s3, "P", "p1"); hs(s3, "real", "p1"); hs(s3, "4", "p1");
    hs(s4, "P", "p2"); hs(s4, "int", "p2");
    hs(s5, "Q", "q");
    hs(s6, "U", "u1"); hs(s6, "7", "u1");
    hs(s7, "U", "u2"); hs(s7, "2", "u2");
    same(s2, s8, "C default vs C#(int,3)");
    same(s4, s9, "pk::P default vs pk::P#(int,1)");
    same(s7, s10, "U default vs U#(2)");
    differ(s1, s2, "C#(real,5) vs C"); differ(s1, s11, "C#(real,5) vs C#(real,6)");
    differ(s3, s4, "P#(real,4) vs P"); differ(s6, s7, "U#(7) vs U");
    differ(s1, s3, "C vs P"); differ(s5, s4, "Q vs P");
    if (errs == 0) $display("PASSED");
  end
endmodule
