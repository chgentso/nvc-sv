// Ports of class handle, string and unpacked structure types (with a
// string member) on a module instantiated twice: input ports follow
// their actuals (a string literal, a class handle, a structure), output
// ports drive their actuals from always and continuous assignments,
// including assignment patterns and string concatenations of input
// ports, and later changes propagate in both directions.
// IEEE 1800-2017 23.2.2.3 (a port may be a variable of any data type),
// 23.3.3.2 / 23.3.3.3 (an input variable port is continuously assigned
// from its actual, an output variable port to it), 10.3.2.
class svportkind2_k; int v; function new(int x); v = x; endfunction endclass
typedef struct { int a; string s; } svportkind2_st;
module svportkind2_sub(input string name, input svportkind2_k kin,
                       output svportkind2_k kout, input svportkind2_st si,
                       output svportkind2_st so, output string tag);
  always @(kin)
    if (kin == null) kout = null;
    else kout = new(kin.v * 2);
  assign so = '{si.a + 1, {si.s, name}};
  assign tag = {name, "_t"};
endmodule
module svportkind2;
  svportkind2_k k1, k2, o1, o2;
  svportkind2_st s1, s2, r1, r2;
  string t1, t2;
  svportkind2_sub a(.name("A"), .kin(k1), .kout(o1), .si(s1), .so(r1), .tag(t1));
  svportkind2_sub b(.name("B"), .kin(k2), .kout(o2), .si(s2), .so(r2), .tag(t2));
  initial begin
    k1 = new(3); k2 = new(5); s1 = '{1, "x"}; s2 = '{10, "y"};
    #1;
    if (o1.v != 6 || o2.v != 10) $fatal(1, "k %0d %0d", o1.v, o2.v);
    if (r1.a != 2 || r1.s != "xA" || r2.a != 11 || r2.s != "yB")
      $fatal(1, "s %p %p", r1, r2);
    if (t1 != "A_t" || t2 != "B_t" || a.name != "A" || b.name != "B")
      $fatal(1, "t %s %s", t1, t2);
    s2.s = "zz"; k1 = null;
    #1;
    if (r2.s != "zzB" || o1 != null) $fatal(1, "upd");
    $display("PASSED");
  end
endmodule
