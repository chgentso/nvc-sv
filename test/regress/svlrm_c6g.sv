module svlrm_c6g;
  // Self-checking test. Expected values: IEEE 1800-2017 6.11 (integer data types, 2-state vs
  // 4-state conversion: x/z become 0 when assigned to a 2-state type), 11.4.6 (bitwise tables),
  // 11.4.7 (logical/reduction), 11.4.5 (==, !=, ===, !==), 11.4.6 (wildcard ==?, !=?), 11.4.3
  // (arithmetic with x gives all x), 11.4.10 (shifts), 11.4.11 (?: with unknown condition combines
  // bitwise), 11.4.12 (concatenation, replication), 20.9 ($isunknown, $countones, $onehot,
  // $onehot0, $clog2), 21.2.1.3 (%h %o %b %d output of x/z: uppercase when only some bits of a
  // digit are unknown, lowercase when all are; %d pads to the width of the type, %0d does not),
  // 5.7.1 (a literal such as 12'o7x7 is zero extended on the left when its leftmost digit is known).
  logic [3:0] a = 4'b10x1, b = 4'b1z01;
  bit [3:0] c;
  integer i;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  `define CKS(fmt, e, txt) ck($sformatf(fmt, e) == txt, `"e`")
  initial begin
    `CKS("%b", a & b, "1001");
    `CKS("%b", a | b, "1xx1");
    `CKS("%b", a ^ b, "0xx0");
    `CKS("%b", ~a, "01x0");
    `CKS("%b", ~^a, "x");
    `CKS("%b", &a, "0");
    `CKS("%b", |a, "1");
    `CKS("%b", ^a, "x");
    `CKS("%b", a == b, "x");
    `CKS("%b", a === b, "0");
    `CKS("%b", a != b, "x");
    `CKS("%b", a !== b, "1");
    `CKS("%b", a ==? 4'b10?1, "1");
    `CKS("%b", a !=? 4'b01?1, "1");
    c = a; `CKS("%b", c, "1001");
    i = a; ck(i[3:0] === 4'b10x1, "4-state integer keeps x");
    ck((a + 1) === {32{1'bx}}, "a + 1 is all x (32-bit context)");
    `CKS("%b", a << 1, "0x10");
    `CKS("%b", a >> 1, "010x");
    `CKS("%b", $isunknown(a), "1");
    `CKS("%b", $isunknown(c), "0");
    `CKS("%0d", $countones(a), "2");
    `CKS("%0d", $countones(4'b1011), "3");
    `CKS("%0d", $onehot(4'b0100), "1");
    `CKS("%0d", $onehot0(4'b0000), "1");
    `CKS("%0d", $clog2(17), "5");
    `CKS("%0d", $clog2(1), "0");
    `CKS("%b", a ? 1'b1 : 1'b0, "1");               // a has a known 1 bit: condition true
    `CKS("%b", (a === 4'bx) ? 1'b1 : 1'b0, "0");
    `CKS("%b", 1'bx ? 4'b1100 : 4'b1010, "1xx0");
    `CKS("%b", {a, b}, "10x11z01");
    `CKS("%b", {2{a}}, "10x110x1");
    `CKS("%b", a[1:0] + b[1:0], "xx");
    `CKS("%b", 4'bz & 4'b0, "0000");
    `CKS("%b", 4'bz | 4'b1, "xxx1");
    `CKS("%b", 4'bx0z1, "x0z1");
    `CKS("%h", 12'bxxxx_0000_zzzz, "x0z");
    `CKS("%h", 12'b1xxx_0000_1111, "X0f");
    `CKS("%d", 4'bx, " x");                          // 4 bits need 2 decimal digits of width
    `CKS("%d", 4'b1x, " X");
    `CKS("%0d", 4'bx, "x");
    `CKS("%0d", 4'bxxxx === 4'bxxxx, "1");
    `CKS("%o", 12'o7x7, "07x7");
    if (errs == 0) $display("PASSED");
  end
endmodule
