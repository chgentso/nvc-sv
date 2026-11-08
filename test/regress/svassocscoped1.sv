// Associative arrays indexed by enums declared in a package and in a class scope (self-checking).
// Expected values: IEEE 1800-2017 7.8 / 7.8.4 (enum index: integral, traversed in ascending value order),
// 7.9.1 (num), 12.7.3 (foreach), 8.25/6.19 (scoped enum names pkg::X, class::A)
package p; typedef enum {X, Y, Z} pe; endpackage
class tr; typedef enum {A, B, C} reg_e; endclass
class sb;
  int r[tr::reg_e];
  string s[p::pe];
  int errs = 0;
  function void f();
    int n;
    r[tr::A] = 3; r[tr::C] = 4;
    s[p::Z] = "z"; s[p::X] = "x";
    n = 0;
    foreach (r[k]) begin
      if (n == 0 && !(k == tr::A && r[k] == 3 && int'(k) == 0)) begin errs++; $error("r first entry"); end
      if (n == 1 && !(k == tr::C && r[k] == 4 && int'(k) == 2)) begin errs++; $error("r second entry"); end
      n++;
    end
    if (n != 2) begin errs++; $error("r count %0d", n); end
    n = 0;
    foreach (s[k]) begin
      if (n == 0 && !(k == p::X && s[k] == "x" && int'(k) == 0)) begin errs++; $error("s first entry"); end
      if (n == 1 && !(k == p::Z && s[k] == "z" && int'(k) == 2)) begin errs++; $error("s second entry"); end
      n++;
    end
    if (n != 2) begin errs++; $error("s count %0d", n); end
  endfunction
endclass
module svassocscoped1;
  int a[p::pe];
  int b[tr::reg_e];
  sb o = new();
  initial begin
    a[p::Y] = 1; b[tr::C] = 2;
    if (a.num() != 1 || b.num() != 1) begin o.errs++; $error("num %0d %0d", a.num(), b.num()); end
    if (!a.exists(p::Y) || a.exists(p::X) || !b.exists(tr::C) || b.exists(tr::A)) begin o.errs++; $error("exists"); end
    o.f();
    if (o.errs == 0) $display("PASSED");
    $finish;
  end
endmodule
