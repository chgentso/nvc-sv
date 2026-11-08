// Self-checking: many format arguments, large struct printed with %p.
// Expected values: IEEE 1800-2017 21.2.1 (%d/%0d/%s/%f of integral, string and real arguments, consumed in order,
// any number of arguments), 21.2.1.7 (%p: only the content is checked, as 21.2.1.7 does not fix the layout - member names and values;
// %0p only the values), 21.3.3 ($sformatf).
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svmanyargs1;
  typedef struct {
    int a0, a1, a2, a3, a4, a5, a6, a7, a8, a9;
    bit [3:0] b0, b1, b2, b3, b4;
    string s0;
    real r0;
    bit [99:0] w0;
    logic [7:0] l0;
    int z0, z1, z2, z3, z4, z5, z6, z7, z8, z9;
  } big_t;
  big_t p;
  string str, pp, p0;
  int errs;
  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++)
      if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    p.a0 = 0; p.a1 = 1; p.a2 = 2; p.a3 = 3; p.a4 = 4; p.a5 = 5; p.a6 = 6; p.a7 = 7; p.a8 = 8; p.a9 = 9;
    p.b0 = 10; p.b1 = 11; p.b2 = 12; p.b3 = 13; p.b4 = 14;
    p.s0 = "str"; p.r0 = 2.5; p.w0 = 100'd123456789012345678901234567890; p.l0 = 8'hx1;
    p.z0 = 20; p.z1 = 21; p.z2 = 22; p.z3 = 23; p.z4 = 24; p.z5 = 25; p.z6 = 26; p.z7 = 27; p.z8 = 28; p.z9 = 29;
    p0 = $sformatf("%0p", p);
    pp = $sformatf("%p", p);
    // member names and values in %p
    `CHK(has(pp, "a0"), 1) `CHK(has(pp, "a9"), 1) `CHK(has(pp, "b4"), 1) `CHK(has(pp, "s0"), 1)
    `CHK(has(pp, "r0"), 1) `CHK(has(pp, "w0"), 1) `CHK(has(pp, "l0"), 1) `CHK(has(pp, "z0"), 1) `CHK(has(pp, "z9"), 1)
    `CHK(has(pp, "str"), 1) `CHK(has(pp, "2.5"), 1) `CHK(has(pp, "123456789012345678901234567890"), 1)
    `CHK(has(pp, "14"), 1) `CHK(has(pp, "29"), 1)
    // %0p carries the same values
    `CHK(has(p0, "str"), 1) `CHK(has(p0, "2.5"), 1) `CHK(has(p0, "123456789012345678901234567890"), 1)
    `CHK(has(p0, "29"), 1) `CHK(has(p0, "14"), 1)
    str = $sformatf("%0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %s %0d %f %0d %0d",
      p.a0, p.a1, p.a2, p.a3, p.a4, p.a5, p.a6, p.a7, p.a8, p.a9, p.b0, p.b1, p.b2, p.b3, p.b4,
      p.z0, p.z1, p.z2, p.z3, p.z4, p.s0, p.z5, p.r0, p.z6, p.w0);
    `CHK(str, "0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 20 21 22 23 24 str 25 2.500000 26 123456789012345678901234567890")
    str = $sformatf("%0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d",
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30);
    `CHK(str, "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30")
    if (errs == 0) $display("PASSED");
  end
endmodule
