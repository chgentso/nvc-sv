// 11.4 operators: arithmetic/relational/logical/bitwise/reduction/shift/conditional/concat/replication/inside
// Expected values: IEEE 1800-2017 11.3.2 (precedence/associativity table, ** is left associative and
// binds below unary operators), 11.4.3 (arithmetic; division truncates toward zero, remainder takes the sign
// of the first operand), 11.4.4-11.4.9 (relational, equality, logical, bitwise, reduction, shift),
// 11.4.11 (conditional with an x condition merges bitwise), 11.4.12 (concatenation/replication),
// 11.4.13 (inside), 11.6 (expression widths), 11.4.5 (=== / == with x)
`define CHK(c, m) if (!(c)) begin errs++; $error(m); end
module svlrm_c11a;
  logic [7:0] a = 8'hc5, b = 8'h3a;
  logic signed [7:0] sa = -8'sd50, sb = 8'sd7;
  int i = 17, j = 5;
  real r = 2.5, q = 0.5;
  int errs = 0;
  logic [7:0] u8;
  logic signed [7:0] s8;
  logic [3:0] u4;
  logic [11:0] u12;
  logic [15:0] u16;
  int vi;
  real vr;
  initial begin
    u8 = a + b; `CHK(u8 == 255, "a+b")
    u8 = a - b; `CHK(u8 == 139, "a-b")
    u8 = a * b; `CHK(u8 == 162, "a*b")
    u8 = a / b; `CHK(u8 == 3, "a/b")
    u8 = a % b; `CHK(u8 == 23, "a%b")
    s8 = sa + sb; `CHK(s8 == -43, "sa+sb")
    s8 = sa - sb; `CHK(s8 == -57, "sa-sb")
    s8 = sa * sb; `CHK(s8 == -94, "sa*sb")
    s8 = sa / sb; `CHK(s8 == -7, "sa/sb")
    s8 = sa % sb; `CHK(s8 == -1, "sa%sb")
    `CHK(i + j == 22 && i - j == 12 && i * j == 85 && i / j == 3 && i % j == 2, "int arith")
    `CHK(-i / j == -3 && -i % j == -2, "neg dividend")
    `CHK(i / -j == -3 && i % -j == 2, "neg divisor")
    vr = r + q; `CHK(vr == 3.0, "r+q")
    vr = r - q; `CHK(vr == 2.0, "r-q")
    vr = r * q; `CHK(vr == 1.25, "r*q")
    vr = r / q; `CHK(vr == 5.0, "r/q")
    `CHK((a > b) === 1'b1 && (a < b) === 1'b0 && (a >= b) === 1'b1 && (a <= b) === 1'b0, "unsigned rel")
    `CHK((sa > sb) === 1'b0 && (sa < sb) === 1'b1, "signed rel")
    `CHK((a == b) === 1'b0 && (a != b) === 1'b1, "eq")
    `CHK((r > q) == 1 && (r == 2.5) == 1, "real rel")
    u8 = a & b; `CHK(u8 === 8'b00000000, "and")
    u8 = a | b; `CHK(u8 === 8'b11111111, "or")
    u8 = a ^ b; `CHK(u8 === 8'b11111111, "xor")
    u8 = ~a; `CHK(u8 === 8'b00111010, "not")
    u8 = a ~^ b; `CHK(u8 === 8'b00000000, "xnor")
    `CHK((^~a) === 1'b1, "reduction xnor (prefix)")
    `CHK((&a) === 1'b0 && (|a) === 1'b1 && (^a) === 1'b0 && (~&a) === 1'b1 && (~|a) === 1'b0 && (~^a) === 1'b1, "reductions")
    `CHK((!a) === 1'b0 && (!0) === 1'b1 && (!5) === 1'b0, "logical not")
    `CHK((a && b) === 1'b1 && (a || 0) === 1'b1 && (0 && 1) === 1'b0, "logical and/or")
    `CHK((a && (b == 8'h3a)) === 1'b1, "logical with ==")
    u8 = a << 2; `CHK(u8 === 8'h14, "a<<2")
    u8 = a >> 2; `CHK(u8 === 8'h31, "a>>2")
    u8 = a >>> 2; `CHK(u8 === 8'h31, "a>>>2 unsigned")
    u8 = sa << 2; `CHK(u8 === 8'h38, "sa<<2")
    u8 = sa >> 2; `CHK(u8 === 8'h33, "sa>>2")
    u8 = sa >>> 2; `CHK(u8 === 8'hf3, "sa>>>2")
    `CHK((i << 3) == 136 && (i >> 2) == 4, "int shifts")
    vi = (a > b) ? 1 : 2; `CHK(vi == 1, "cond 1")
    u8 = (a > b) ? a : b; `CHK(u8 === 8'hc5, "cond 2")
    s8 = (sa < 0) ? sa : sb; `CHK(s8 == -50, "cond 3")
    u16 = {a, b}; `CHK(u16 === 16'hc53a, "concat")
    u8 = {a[3:0], b[7:4]}; `CHK(u8 === 8'h53, "concat parts")
    u16 = {2{a}}; `CHK(u16 === 16'hc5c5, "replicate")
    u12 = {a, {2{b[1:0]}}}; `CHK(u12 === 12'hc5a, "concat replicate")
    `CHK((a inside {8'hc5, 8'h00}) === 1'b1, "inside 1")
    `CHK((a inside {[8'h00:8'h10]}) === 1'b0, "inside 2")
    `CHK((a inside {[8'hc0:8'hcf]}) === 1'b1, "inside 3")
    `CHK((i inside {1, 2, [10:20]}) === 1'b1, "inside 4")
    `CHK((3 inside {1, 2}) === 1'b0, "inside 5")
    `CHK((2 ** 3 ** 2) == 64 && ((2 ** 3) ** 2) == 64, "power")
    `CHK((-2 ** 2) == 4, "unary minus before power")
    `CHK((1 + 2 * 3 - 4 / 2) == 5, "prec arith")
    `CHK((1 << 2 + 1) == 8, "prec shift")
    `CHK((8 >> 1 + 1) == 2, "prec shift 2")
    `CHK((5 & 3 | 4 ^ 1) == 5, "prec bitwise")
    `CHK((1 < 2 == 1) == 1, "prec rel/eq")
    `CHK((4 > 3 > 2) == 0, "rel left assoc")
    `CHK((~5 & 7) == 2, "~5&7")
    `CHK((-5 + 3) == -2, "-5+3")
    `CHK((!1 + 1) == 1, "!1+1")
    `CHK((1 ? 2 : 3 ? 4 : 5) == 2, "cond chain 1")
    `CHK((0 ? 2 : 0 ? 4 : 5) == 5, "cond chain 2")
    u4 = 1'b1 ? 4'b1100 : 4'b1010; `CHK(u4 === 4'b1100, "cond true")
    u4 = 1'bx ? 4'b1100 : 4'b1010; `CHK(u4 === 4'b1xx0, "cond x merge")
    u4 = 1'bx ? 4'b1100 : 4'b1100; `CHK(u4 === 4'b1100, "cond x same")
    `CHK((5 == 5.0) === 1'b1 && (5 === 5) === 1'b1, "int/real eq")
    `CHK((4'b1x01 == 4'b1x01) === 1'bx, "== with x")
    `CHK((4'b1x01 === 4'b1x01) === 1'b1, "=== with x")
    `CHK((7 / 0) === 32'bx, "7/0")
    `CHK((7 % 0) === 32'bx, "7%0")
    if (errs == 0) $display("PASSED");
  end
endmodule
