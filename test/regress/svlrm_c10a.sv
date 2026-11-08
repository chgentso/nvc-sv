// 10.3-10.10 continuous assign, blocking/nonblocking, assignment patterns, ordering
// Expected values: IEEE 1800-2017 10.3 (continuous assignment, delays), 10.4.1 / 10.4.2 (blocking and
// nonblocking assignment: NBA update in the NBA region, last NBA to a variable wins), 4.4 / 4.5
// (#0 resumes before the NBA region), 10.9 / 11.4.12 (concatenation on the left), 11.4.1 (assignment
// operators, ++/--), 11.4.10 (shifts incl. arithmetic), 11.5.1 (bit/part-select index expressions)
`timescale 1ns/1ns
`define CHK(c, m) if (!(c)) begin errs++; $error(m); end
module svlrm_c10a;
  logic [3:0] a, b, c;
  wire [3:0] w1, w2;
  assign w1 = a + b;
  assign #2 w2 = a & b;
  wire [3:0] w3 = ~a;
  logic [7:0] ab;
  int i, j, k;
  int errs = 0;
  initial begin
    a = 4'h3; b = 4'h5;
    #1;
    `CHK(w1 === 4'h8 && w3 === 4'hc, "t=1 w1/w3")
    #2;
    `CHK(w1 === 4'h8 && w2 === 4'h1, "t=3 w1/w2")
    a = 4'hf;
    #1;
    `CHK(w1 === 4'h4 && w2 === 4'h1 && w3 === 4'h0, "t=4 w1/w2/w3")
    #2;
    `CHK(w2 === 4'h5, "t=6 w2")
    {a, b} = 8'h5a;
    `CHK(a === 4'h5 && b === 4'ha, "concat lhs")
    {a[1:0], b[3:2]} = 4'b1001;
    `CHK(a === 4'b0110 && b === 4'b0110, "concat lhs parts")
    ab = {a, b};
    `CHK(ab === 8'h66, "ab")
    {i, j} = {32'd7, 32'd9};
    `CHK(i == 7 && j == 9, "int concat")
    // i++ + ++i: 5 + 7 or 6 + 6 depending on operand order, both 12; i ends as 7
    i = 5; j = i++ + ++i;
    `CHK(i == 7 && j == 12, "i++ + ++i")
    k = (i = 3) + 1;
    `CHK(i == 3 && k == 4, "assign expr")
    i = 10; i += 5; i -= 3; i *= 2; i /= 4; i %= 4;
    `CHK(i == 2, "compound arith")
    i = 6; i <<= 2; i >>= 1; i |= 1; i &= 7; i ^= 2;
    `CHK(i == 7, "compound bit")
    i = -16; i >>>= 2;
    `CHK(i == -4, ">>>=")
    i = 1; i <<<= 3;
    `CHK(i == 8, "<<<=")
    // a, b are 6, 6 here
    a <= 4'h1; b <= 4'h2;
    `CHK(a === 4'h6 && b === 4'h6, "NBA not yet visible")
    #0;
    `CHK(a === 4'h6 && b === 4'h6, "NBA not visible after #0")
    a <= #3 4'h8;
    #2;
    `CHK(a === 4'h1, "a after 2")
    #2;
    `CHK(a === 4'h8, "a after 4")
    {a, b} <= 8'hab; #1;
    `CHK(a === 4'ha && b === 4'hb, "concat NBA")
    a = 4'h1; a <= 4'h2; a = 4'h3; #1;
    `CHK(a === 4'h2, "NBA after later blocking")
    a <= 4'h4; a <= 4'h5; #1;
    `CHK(a === 4'h5, "last NBA wins")
    k = 3; a = 4'hf; a[k] = 0;
    `CHK(a === 4'b0111, "bit select write")
    a[k -: 2] = 2'b01;
    `CHK(a === 4'b0111, "indexed part select write")
    i = 0; a = 0; a[i++] = 1; a[i++] = 1;
    `CHK(a === 4'b0011 && i == 2, "index side effects")
    if (errs == 0) $display("PASSED");
  end
endmodule
