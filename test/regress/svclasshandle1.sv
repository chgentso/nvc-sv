// Expected values: IEEE 1800-2017 8.4 / 8.7 / 8.8 (class handles: allocation with new, assignment,
// null, upcast), 11.4.5 (handle equality), 21.2.1.7 (%p prints values as assignment patterns).
// The LRM defines no integral value for a handle, so %d/%x/%h are NOT applied to handles.
// Only the content of %p is checked (member name and value, `null`), never its exact layout.
class C; int x; endclass
class D extends C; endclass

module svclasshandle1;
  C c, d, n;
  D e;
  C m;
  int errors = 0;

  function automatic string squash(string s);
    string r = "";
    for (int i = 0; i < s.len(); i++) if (s[i] != " ") r = {r, s.getc(i)};
    return r;
  endfunction

  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++)
      if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction

  initial begin
    string p;
    c = new; d = new; e = new;
    if (c == null || d == null || e == null) begin $error("new returned null"); errors++; end
    if (n != null) begin $error("uninitialised handle is not null"); errors++; end
    if (c == d) begin $error("two objects compare equal"); errors++; end
    if (c == e) begin $error("C and D objects compare equal"); errors++; end
    m = e;
    if (m != e || e != m) begin $error("upcast handle differs from the original"); errors++; end
    m = c;
    if (m != c) begin $error("copied handle differs"); errors++; end
    // aliasing: a write through one handle is seen through the other
    c.x = 5;
    if (m.x !== 5) begin $error("m.x = %0d, expected 5", m.x); errors++; end
    if (d.x !== 0) begin $error("d.x = %0d, expected 0 (distinct object)", d.x); errors++; end
    // %p content
    p = squash($sformatf("%p", c));
    if (!has(p, "x:5")) begin $error("%%p of handle lacks member x:5: %s", p); errors++; end
    p = squash($sformatf("%p", n));
    if (!has(p, "null")) begin $error("%%p of null handle lacks null: %s", p); errors++; end
    c = null;
    if (c != null) begin $error("c not null after c = null"); errors++; end
    if (m.x !== 5) begin $error("object lost after dropping one handle"); errors++; end
    c = new;
    if (c == m) begin $error("new object equals old one"); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
