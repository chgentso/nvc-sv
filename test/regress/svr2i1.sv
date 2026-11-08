// Expected values: IEEE 1800-2017 6.12.2 / 6.24.1 (real to integral
// conversion rounds to nearest, ties away from zero), 20.5 ($rtoi truncates,
// $itor), 11.8.1 (signed/unsigned operand types), 6.11 (time is unsigned
// 64-bit).  Out-of-range real->integer results, NaN and infinity are
// implementation-defined and are deliberately not checked.
module svr2i1;
  real r; int i; longint l; shortint si; byte by; bit [63:0] u64; bit [31:0] u32; logic [15:0] u16; time t; integer ig; bit [7:0] u8; logic [127:0] w;
  int errs;
  task automatic ck(longint got, longint exp, string what);
    if (got !== exp) begin $error("%s: got %0d expected %0d", what, got, exp); errs++; end
  endtask
  task automatic ckr(real got, real exp, string what);
    if (got != exp) begin $error("%s: got %f expected %f", what, got, exp); errs++; end
  endtask
  initial begin
    r = 2.0**31 - 1; i = r; ck(i, 64'sd2147483647, "int max");
    r = -2.0**31; i = r; ck(i, -64'sd2147483648, "int min");
    r = 2.0**32 + 5; l = r; ck(l, 64'sd4294967301, "2^32+5 -> longint");
    r = 1e10; l = r; ck(l, 64'sd10000000000, "1e10 -> longint");
    r = 2.0**62; l = r; ck(l, 64'sh4000_0000_0000_0000, "2^62 -> longint");
    r = -2.0**63; l = r; ck(l, 64'sh8000_0000_0000_0000, "-2^63 -> longint");
    r = 1.5; i = r; ck(i, 2, "1.5");
    r = 2.5; i = r; ck(i, 3, "2.5");
    r = -1.5; i = r; ck(i, -2, "-1.5");
    r = -2.5; i = r; ck(i, -3, "-2.5");
    r = 0.49; i = r; ck(i, 0, "0.49");
    r = -0.49; i = r; ck(i, 0, "-0.49");
    r = 300.7; i = r; ck(i, 301, "300.7");
    r = -300.2; i = r; ck(i, -300, "-300.2");
    r = 100.7; si = r; ck(si, 101, "100.7 -> shortint");
    r = 100.7; by = r; ck(by, 101, "100.7 -> byte");
    r = -100.2; by = r; ck(by, -100, "-100.2 -> byte");
    r = 1e19; u64 = r; ck(u64, 64'd10000000000000000000, "1e19 -> bit[63:0]");
    r = 2.0**63; u64 = r; ck(u64, 64'h8000_0000_0000_0000, "2^63 -> bit[63:0]");
    r = 3e9; u32 = r; ck(u32, 64'sd3000000000, "3e9 -> bit[31:0]");
    r = 5e9; ig = 0; l = r; ck(l, 64'sd5000000000, "5e9 -> longint");
    r = 40000.5; u16 = r; ck(u16, 40001, "40000.5 -> logic[15:0]");
    r = 1e19; t = r; ck(t, 64'd10000000000000000000, "1e19 -> time");
    r = 200.5; u8 = r; ck(u8, 201, "200.5 -> bit[7:0]");
    r = 12345.5; ig = r; ck(ig, 12346, "12345.5 -> integer");
    r = 2.0**100; w = r; if (w !== 128'h1 << 100) begin $error("2^100 -> logic[127:0]: got %h", w); errs++; end
    t = 64'hffff_ffff_ffff_ffff; ck(t, -1, "time all ones");
    if ((t + 0) !== 64'hffff_ffff_ffff_ffff) begin $error("t + 0 not unsigned 64-bit"); errs++; end
    t = -1; r = t; ckr(r, 2.0**64, "time -1 -> real");
    u64 = 64'hffff_ffff_ffff_ffff; r = u64; ckr(r, 2.0**64, "u64 max -> real");
    l = -1; r = l; ckr(r, -1.0, "longint -1 -> real");
    u32 = 32'hffff_ffff; r = u32; ckr(r, 4294967295.0, "u32 max -> real");
    i = -1; r = i; ckr(r, -1.0, "int -1 -> real");
    ck(longint'(2.0**33), 64'sd8589934592, "longint'(2^33)");
    ck(longint'(1e18), 64'sd1000000000000000000, "longint'(1e18)");
    ck(int'(2.5), 3, "int'(2.5)");
    ck(int'(-2.5), -3, "int'(-2.5)");
    ck($rtoi(3.99), 3, "$rtoi(3.99)");
    ck($rtoi(-3.99), -3, "$rtoi(-3.99)");
    ck($rtoi(1e9), 1000000000, "$rtoi(1e9)");
    ck($rtoi(-2e9), -2000000000, "$rtoi(-2e9)");
    if (errs == 0) $display("PASSED");
  end
endmodule
