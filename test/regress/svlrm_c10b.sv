// assignment extension/truncation/sign, mixed types, 10.8 assignment-like contexts
// Expected values: IEEE 1800-2017 10.7 (assignment extension and truncation; extension follows the
// signedness of the right-hand side), 6.11.3 / 6.12.2 (real to integer conversion rounds to nearest, ties
// away from zero, then the integer is truncated to the target width), 6.11 (2-state types take 0 for x/z),
// 11.6 (expression bit lengths: context-determined operands), 11.4.3 (arithmetic wraps to the width)
`define CHK(c, m) if (!(c)) begin errs++; $error(m); end
module svlrm_c10b;
  logic [3:0] a4; logic [7:0] a8; logic signed [3:0] s4; logic signed [7:0] s8;
  bit [3:0] b4; byte by; int i; longint l; shortint si; integer ig; real r; time t;
  int errs = 0;
  initial begin
    a8 = 8'hff; a4 = a8; `CHK(a4 === 4'hf, "01")
    a4 = 4'hf; a8 = a4; `CHK(a8 === 8'h0f, "02")
    s4 = 4'hf; s8 = s4; `CHK(s8 === 8'hff && s8 == -1, "03")
    s4 = 4'hf; a8 = s4; `CHK(a8 === 8'hff, "04")
    a4 = 4'hf; s8 = a4; `CHK(s8 === 8'h0f, "05")
    by = 8'hff; i = by; `CHK(i == -1, "06")
    by = -1; l = by; `CHK(l == -1, "07")
    b4 = 4'hf; i = b4; `CHK(i == 15, "08")
    i = -1; l = i; `CHK(l == -1, "09")
    i = 32'hffffffff; l = i; `CHK(l == -1, "10")
    l = 64'hffffffff; i = l; `CHK(i == -1, "11")
    i = 300; by = i; `CHK(by == 44, "12")
    si = 32768; `CHK(si == -32768, "13")
    si = i; `CHK(si == 300, "14")
    ig = 'x; i = ig; `CHK(i == 0, "15")
    ig = 'x; a4 = ig; `CHK(a4 === 4'bxxxx, "16")
    a4 = 4'bx01z; b4 = a4; `CHK(b4 === 4'b0010, "17")
    r = 3.7; i = r; `CHK(i == 4, "18")
    r = -3.7; i = r; `CHK(i == -4, "19")
    r = 3.5; i = r; by = r; `CHK(i == 4 && by == 4, "20")
    i = 7; r = i; `CHK(r == 7.0, "21")
    r = 1e10; i = r; `CHK(i == 1410065408, "22")
    r = 1e20; l = r; `CHK(l == 64'sd7766279631452241920, "23")
    r = -1e20; l = r; `CHK(l == -64'sd7766279631452241920, "24")
    r = 1.5; a4 = r; `CHK(a4 === 4'd2, "25")
    t = 64'hffff_ffff_ffff; `CHK(t == 64'd281474976710655, "26")
    t = -1; `CHK(t === 64'hffff_ffff_ffff_ffff, "27")
    r = t; `CHK(r == 18446744073709551616.0, "28")
    l = 64'h8000_0000_0000_0000; r = l; `CHK(r == -9223372036854775808.0, "29")
    i = 2**31; `CHK(i == -2147483648, "30")
    i = -2**31; `CHK(i == -2147483648, "31")
    l = 2**63; `CHK(l == 64'sh8000_0000_0000_0000, "32")
    i = 32'h7fffffff; i = i + 1; `CHK(i == -2147483648, "33")
    by = 127; by = by + 1; `CHK(by == -128, "34")
    a4 = 4'd9; a4 = a4 + 4'd9; `CHK(a4 === 4'd2, "35")
    a4 = 9 + 9; `CHK(a4 === 4'd2, "36")
    a8 = 200; a8 = a8 + 100; `CHK(a8 === 8'd44, "37")
    a8 = (200 + 100) / 2; `CHK(a8 === 8'd150, "38")
    a8 = (a8 + 100) / 2; `CHK(a8 === 8'd125, "39")
    if (errs == 0) $display("PASSED");
  end
endmodule
