// Expected values: IEEE 1800-2017 7.3.1 (unpacked union: the member last written is defined; inactive members are
// not), 7.3.1 (packed union: all members share the same bits, member 0 is the most significant alignment),
// 21.2.1.7 (%p of an unpacked union shows its first member)
// Reads of an inactive member of an unpacked union are not checked (tool-defined storage).
module svunionwidth1;
  typedef union { bit [63:0] b; int i; byte c; } u_t;
  typedef union packed { bit [63:0] b; struct packed { bit [31:0] hi, lo; } h; } pu_t;
  u_t u;
  pu_t p;
  int errs = 0;
  string t;
  function automatic bit has(string hay, string needle);
    for (int k = 0; k + needle.len() <= hay.len(); k++)
      if (hay.substr(k, k + needle.len() - 1) == needle) return 1;
    return 0;
  endfunction
  initial begin
    u.b = 64'h1122334455667788;
    if (u.b !== 64'h1122334455667788) begin errs++; $error("u.b=%h", u.b); end
    u.i = 32'hdeadbeef;
    if (u.i !== 32'hdeadbeef) begin errs++; $error("u.i=%h", u.i); end
    u.c = 8'h5a;
    if (u.c !== 8'h5a) begin errs++; $error("u.c=%h", u.c); end
    u.b = 64'h1122334455667788;
    t = $sformatf("%p", u);
    if (!has(t, "b") || !(has(t, "1122334455667788") || has(t, "1234605616436508552"))) begin
      errs++; $error("%%p of union does not show first member b: %s", t);
    end
    // packed union: members overlay exactly (7.3.1)
    p.b = 64'h1122334455667788;
    if (p.h.hi !== 32'h11223344 || p.h.lo !== 32'h55667788) begin
      errs++; $error("packed union overlay: hi=%h lo=%h", p.h.hi, p.h.lo);
    end
    p.h.lo = 32'hdeadbeef;
    if (p.b !== 64'h11223344deadbeef) begin errs++; $error("packed union b=%h", p.b); end
    if (errs == 0) $display("PASSED");
  end
endmodule
