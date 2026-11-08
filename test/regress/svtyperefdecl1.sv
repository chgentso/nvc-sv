// Expected values: IEEE 1800-2017 6.22 (type equivalence), 6.23 (type operator), 6.20 (constants), 6.24.1 (casting; signedness is preserved by a size cast),
// 11.6 / 11.8.2 (expression width), 20.6.2 ($bits), 7.6 (array assignment), 9.3 / 23.8 (scope and hierarchical names)
module svtyperefdecl1;
  typedef bit [7:0] byte_t;
  typedef byte_t arr_t[3];
  parameter int P = 5;
  localparam bit [P-1:0] LP = '1;
  const int K = 7;
  byte_t a, b;
  var type(a) c;
  type(a) d;
  int i;
  typedef logic [3:0] nib;
  nib n = 4'ha;
  arr_t ar = '{1,2,3};
  type(ar) ar2;
  int errs = 0;
  logic [15:0] r16;
  task automatic ck(string what, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d expected %0d", what, got, exp); end
  endtask
  initial begin
    a = 8'd200; b = a; c = a + 1; d = c;
    ck("a", a, 200); ck("b", b, 200); ck("c", c, 201); ck("d", d, 201);
    ck("bits c", $bits(c), 8); ck("bits type(ar)", $bits(type(ar)), 24);
    ck("LP", LP, 31); ck("K", K, 7);
    ar2 = ar; ar2[1] = 9;
    ck("ar2[0]", ar2[0], 1); ck("ar2[1]", ar2[1], 9); ck("ar2[2]", ar2[2], 3);
    ck("ar unchanged", ar[1], 2);
    ck("type a==b", type(a) == type(b), 1);
    ck("type a==i", type(a) == type(i), 0);
    ck("type n", type(n) == type(logic [3:0]), 1);
    ck("type ===", type(a) === type(bit [7:0]), 1);
    begin
      automatic byte_t x = 8'd1;
      automatic type(x) y = 8'd2;
      ck("x+y", x + y, 3);
    end
    i = 3;
    begin : blk
      automatic int i = 10;
      ck("blk.i", i, 10); ck("outer i", svtyperefdecl1.i, 3);
    end
    ck("i after blk", i, 3);
    ck("int'(a)+int'(b)", int'(a) + int'(b), 400);
    ck("shortint'", shortint'(32'h12345678), 22136);
    r16 = 16'(8'sh80);
    if (r16 !== 16'hff80) begin errs++; $error("16'(8'sh80)=%h", r16); end
    r16 = 16'(signed'(8'h80));
    if (r16 !== 16'hff80) begin errs++; $error("16'(signed'(8'h80))=%h", r16); end
    // size cast keeps the signedness of its operand: both operands are signed 8-bit (-3 and -6), sum -3 in 8 bits
    ck("8'(i)+8'(250)", 8'(i) + 8'(250), -3);
    ck("(a+b)/2", (a + b) / 2, 200);
    ck("9'(a+b)/2", 9'(a + b) / 2, 200);
    if (errs == 0) $display("PASSED");
  end
endmodule
