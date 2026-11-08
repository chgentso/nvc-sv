// Expected values: IEEE 1800-2017 7.3.2 (tagged unions, packed and unpacked),
// 11.9 (tagged union expressions, 'matches', member access), 21.2.1.7 (%p).
// The text produced by %p / %0p for tagged unions (and whether a 4-state
// value prints with x/z digits) is implementation-defined and is not
// compared.  Checked instead: each assignment stores the tag and the value
// (read back with matches and member access, 4-state values compared with
// ===), and %p / %0p of every object produce a non-empty string; for the
// unpacked union holding a string, the string occurs in the %p output.
module svtaggedfmt1;
  typedef union tagged packed {
    bit [7:0] a;
    bit [3:0] b;
    void n;
    struct packed { bit [3:0] x; bit [3:0] y; } s;
  } p_t;
  typedef struct { p_t f; int k; } st_t;
  typedef union tagged packed { logic [7:0] a; logic [3:0] b; } q_t;
  typedef union tagged { int i; string s; void n; } u_t;
  p_t p;
  st_t st;
  q_t q;
  u_t u;
  int errs;
  function automatic bit has(string str, string sub);
    for (int i = 0; i + sub.len() <= str.len(); i++) if (str.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  task automatic chk(bit c, string m); if (!c) begin $error("%s", m); errs++; end endtask
  task automatic nonempty(string what, string s1, string s2);
    chk(s1.len() > 0 && s2.len() > 0, {"%p / %0p of ", what, " is empty"});
  endtask
  initial begin
    nonempty("p (initial)", $sformatf("%p", p), $sformatf("%0p", p));
    p = tagged a 8'h5a;
    chk(p matches tagged a ? 1 : 0, "p tag a"); chk(p.a == 8'h5a, "p.a");
    nonempty("p=a", $sformatf("%p", p), $sformatf("%0p", p));
    p = tagged b 4'h3;
    chk(p matches tagged b ? 1 : 0, "p tag b"); chk(p.b == 4'h3, "p.b");
    nonempty("p=b", $sformatf("%p", p), $sformatf("%0p", p));
    p = tagged n;
    chk(p matches tagged n ? 1 : 0, "p tag n");
    nonempty("p=n", $sformatf("%p", p), $sformatf("%0p", p));
    p = tagged s 8'h12;
    chk(p matches tagged s ? 1 : 0, "p tag s"); chk(p.s.x == 4'h1 && p.s.y == 4'h2, "p.s");
    nonempty("p=s", $sformatf("%p", p), $sformatf("%0p", p));
    st.f = tagged a 8'h01;
    st.k = 3;
    chk(st.f matches tagged a ? 1 : 0, "st.f tag a"); chk(st.f.a == 8'h01 && st.k == 3, "st members");
    nonempty("st", $sformatf("%p", st), $sformatf("%0p", st));
    q = tagged a 8'hx5;
    chk(q matches tagged a ? 1 : 0, "q tag a"); chk(q.a === 8'hx5, "q.a === 8'hx5");
    nonempty("q=a", $sformatf("%p", q), $sformatf("%0p", q));
    q = tagged b 4'hz;
    chk(q matches tagged b ? 1 : 0, "q tag b"); chk(q.b === 4'hz, "q.b === 4'hz");
    nonempty("q=b", $sformatf("%p", q), $sformatf("%0p", q));
    nonempty("u (initial)", $sformatf("%p", u), $sformatf("%0p", u));
    u = tagged s "hi";
    chk(u matches tagged s ? 1 : 0, "u tag s"); chk(u.s == "hi", "u.s");
    nonempty("u=s", $sformatf("%p", u), $sformatf("%0p", u));
    chk(has($sformatf("%p", u), "hi"), {"%p of u holding \"hi\": ", $sformatf("%p", u)});
    u = tagged i 7;
    chk(u matches tagged i ? 1 : 0, "u tag i"); chk(u.i == 7, "u.i");
    u = tagged n;
    chk(u matches tagged n ? 1 : 0, "u tag n");
    nonempty("u=n", $sformatf("%p", u), $sformatf("%0p", u));
    if (errs == 0) $display("PASSED");
  end
endmodule
