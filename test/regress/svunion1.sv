// Expected values: IEEE 1800-2017 7.3 (unions), 7.3.1 (packed unions: all
// members have the same width and overlay the same bits, first member
// leftmost/most significant layout of packed arrays and structs per 7.2.1 and
// 7.4.1), 7.2.1 (packed structure layout), 6.9.1 (packed arrays, index 0 is
// the least significant element), 5.7 (hex literals with x).
// The bit layout of an UNPACKED union (how one member aliases another) is not
// defined by the LRM, so for unpacked unions only reading back the member
// (and element) that was written is checked.  Packed unions, packed structs
// and packed arrays are checked bit-exactly.
module svunion1;
  typedef union { int a; byte b[4]; shortint s[2]; } u1_t;
  typedef union { logic [15:0] w; bit [7:0] b[2]; } u2_t;
  typedef union { bit [3:0] n; bit [3:0] m[1]; } u4_t;
  typedef union { int a; byte b[3:0]; bit [7:0] c[2][2]; } u5_t;
  typedef union packed { logic [15:0] w; bit [1:0][7:0] b; struct packed {bit [7:0] hi, lo;} s; } pu_t;
  typedef struct { int x; u1_t u; } sw_t;
  typedef struct packed { bit [1:0][3:0] n; logic [7:0] v; } ps_t;
  u1_t u1; u2_t u2; u4_t u4; u5_t u5; pu_t pu; sw_t sw; ps_t ps = 16'h1234;
  bit [1:0][7:0] pa = 16'hA5C3;
  logic [3:0][3:0] la = 16'h12x4;
  int errs;
  task automatic ck(logic [31:0] got, logic [31:0] exp, string what);
    if (got !== exp) begin $error("%s: got %h expected %h", what, got, exp); errs++; end
  endtask
  initial begin
    u1.a = 32'h11223344;
    ck(u1.a, 32'h11223344, "u1.a");
    u1.b[3] = 8'hAB; ck(u1.b[3], 8'hAB, "u1.b[3]");
    u1.s[1] = 16'hCDEF; ck(u1.s[1], 16'hCDEF, "u1.s[1]");
    u2.w = 16'hA5C3; ck(u2.w, 16'hA5C3, "u2.w");
    u2.b[1] = 8'h12; ck(u2.b[1], 8'h12, "u2.b[1]");
    u4.n = 4'h9; ck(u4.n, 4'h9, "u4.n");
    u4.m[0] = 4'h6; ck(u4.m[0], 4'h6, "u4.m[0]");
    sw.x = 7; sw.u.a = 32'hdeadbeef; ck(sw.x, 7, "sw.x"); ck(sw.u.a, 32'hdeadbeef, "sw.u.a");
    u5.a = 32'h11223344; ck(u5.a, 32'h11223344, "u5.a");
    u5.b[2] = 8'h5A; ck(u5.b[2], 8'h5A, "u5.b[2]");
    u5.c[1][0] = 8'hC3; ck(u5.c[1][0], 8'hC3, "u5.c[1][0]");
    // packed union: members overlay the same 16 bits
    ck($bits(pu), 16, "$bits(packed union)");
    pu.w = 16'hA5C3;
    ck(pu.b[0], 8'hC3, "pu.b[0]"); ck(pu.b[1], 8'hA5, "pu.b[1]");
    ck(pu.s.hi, 8'hA5, "pu.s.hi"); ck(pu.s.lo, 8'hC3, "pu.s.lo");
    pu.b[1] = 8'h12; ck(pu.w, 16'h12C3, "pu.w after pu.b[1] = 12");
    pu.s.lo = 8'h34; ck(pu.w, 16'h1234, "pu.w after pu.s.lo = 34");
    pu.w = 16'hxxA5; ck(pu.b[0], 8'hA5, "pu.b[0] of A5");  // (2-state members aliasing x bits are not checked)
    // packed struct / arrays
    ck(ps.n[1], 4'h1, "ps.n[1]"); ck(ps.n[0], 4'h2, "ps.n[0]"); ck(ps.v, 8'h34, "ps.v");
    ck(ps, 16'h1234, "ps as a whole");
    ck(pa[1], 8'hA5, "pa[1]"); ck(pa[0], 8'hC3, "pa[0]");
    ck(la[3], 4'h1, "la[3]"); ck(la[2], 4'h2, "la[2]"); ck(la[1], 4'hx, "la[1]"); ck(la[0], 4'h4, "la[0]");
    if (errs == 0) $display("PASSED");
  end
endmodule
