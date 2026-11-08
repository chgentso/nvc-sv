// Expected values: IEEE 1800-2017 12.7.1 (for with declarations in the init list),
// 6.19.5.1-6.19.5.5 (enum first/last/next/prev/num/name), 6.20.3 (type parameters).
typedef enum logic [1:0] {A=0, B=1, C=2} e_t;
module sub #(parameter type StateEnumT = e_t);
  int n = 0;
  int bad = 0;
  StateEnumT z;
  initial begin
    for (int i = 0, StateEnumT t = t.first(); i < t.num(); i += 1, t = t.next()) begin
      // enumerators are numbered consecutively from 0 in both e_t and f_t
      if (int'(t) !== i) begin $error("sub: i=%0d t=%0d", i, int'(t)); bad++; end
      n++;
    end
    if (n != z.num()) begin $error("sub: n=%0d num=%0d", n, z.num()); bad++; end
  end
endmodule
module tsub #(parameter type T = logic [1:0]);
  T x;
  int bad = 0;
  initial begin
    x = x.first();
    if (int'(x) !== 0 || x.num() != 3) begin $error("tsub first/num"); bad++; end
    x = x.last();
    if (int'(x) !== 2 || x.name() != "C") begin $error("tsub last/name"); bad++; end
  end
endmodule
module svforinit1;
  typedef enum {X, Y} f_t;
  sub u();
  sub #(f_t) v();
  tsub #(e_t) w();
  string nm[2];
  initial begin
    for (int i = 0, e_t t = t.last(); i < 2; i++, t = t.prev()) nm[i] = t.name();
    #1;
    if (u.n != 3 || v.n != 2) $error("iteration counts u=%0d v=%0d", u.n, v.n);
    if (nm[0] != "C" || nm[1] != "B") $error("prev walk %s %s", nm[0], nm[1]);
    if (u.bad + v.bad + w.bad != 0 || u.n != 3 || v.n != 2 || nm[0] != "C" || nm[1] != "B")
      $display("FAILED");
    else
      $display("PASSED");
  end
endmodule
