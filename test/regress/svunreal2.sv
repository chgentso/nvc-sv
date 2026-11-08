// Expected values: IEEE 1800-2017 7.3.1 (unpacked unions: only the member last written is defined; copying a whole
// union of the same type is defined), 11.4.2 (++), 6.12 (real arithmetic), 20.6.1 ($typename of a real is "real"),
// 21.2.1.7 (%p: the first member of an unpacked union is shown)
// Reads of an inactive member of an unpacked union are not checked (tool-defined storage).
module svunreal2;
  typedef union { real r; bit [63:0] b; int i; } u_t;
  typedef struct { u_t u; int tag; } s_t;
  u_t u, w; s_t s; real x;
  int errs = 0;
  string t;
  function automatic bit has(string hay, string needle);
    for (int k = 0; k + needle.len() <= hay.len(); k++)
      if (hay.substr(k, k + needle.len() - 1) == needle) return 1;
    return 0;
  endfunction
  initial begin
    u.r = 1.5;
    t = $sformatf("%p", u);
    if (!has(t, "1.5")) begin errs++; $error("%%p of union lacks 1.5: %s", t); end
    t = $sformatf("%p", s);
    if (!has(t, "tag") || !has(t, "0")) begin errs++; $error("%%p of default struct: %s", t); end
    s.u.r = 2.25; s.tag = 7;
    t = $sformatf("%p", s);
    if (!has(t, "2.25") || !has(t, "tag") || !has(t, "7")) begin errs++; $error("%%p of struct: %s", t); end
    w = u;
    if (!(w.r == 1.5)) begin errs++; $error("copied union w.r=%f", w.r); end
    x = u.r * 2 + 1;
    if (!(x == 4.0)) begin errs++; $error("x=%f", x); end
    if (!(u.r > 1.0)) begin errs++; $error("u.r > 1.0 false"); end
    u.b = 64'h3ff0000000000000;
    if (u.b !== 64'h3ff0000000000000) begin errs++; $error("u.b=%h", u.b); end
    u.r = 1.0;
    u.r += 1.0;
    if (!(u.r == 2.0)) begin errs++; $error("u.r after += is %f", u.r); end
    u.r = 0.5; u.r++;
    if (!(u.r == 1.5)) begin errs++; $error("u.r after ++ is %f", u.r); end
    u.i = 0;
    if (u.i !== 0) begin errs++; $error("u.i=%0d", u.i); end
    u.r = 0.0;
    if (!(u.r == 0.0)) begin errs++; $error("u.r != 0.0"); end
    if ($typename(u.r) != "real") begin errs++; $error("typename=%s", $typename(u.r)); end
    if (errs == 0) $display("PASSED");
  end
endmodule
