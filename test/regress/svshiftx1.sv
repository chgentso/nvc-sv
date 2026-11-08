// Expected values: IEEE 1800-2017 11.4.10 (shift operators: << <<< and >> shift in zeros, >>> shifts in the sign bit when
// the left operand is signed; x/z bits move with the shift; a shift amount with x or z gives an all-x result; the
// shift amount is unsigned; shifting by at least the width gives all zeros, or all sign bits for signed >>>),
// 11.8.1 (a signed shift is only arithmetic if the whole expression is signed, so signed results are compared with
// signed literals), 5.7.1 (x/z literal digits)
module svshiftx1;
  logic [3:0] a = 4'b10x1;
  logic [3:0] r;
  logic signed [3:0] sa = 4'b1x01;
  logic signed [3:0] sz = 4'bz001;
  logic [99:0] w = {40'bx, 40'h0, 20'b1z0};
  logic signed [99:0] sw = {1'bx, 99'h1};
  logic [99:0] wr;
  logic [7:0] b8 = 8'hf0;
  int n = 1, m = 70, big = 100;
  logic [2:0] sh = 3'bx00;
  int err = 0;
  function automatic void chk(bit ok, string msg);
    if (!ok) begin $error("%s", msg); err++; end
  endfunction
  initial begin
    r = a << 1;  chk(r === 4'b0x10, "a << 1");
    r = a >> 1;  chk(r === 4'b010x, "a >> 1");
    r = a << n;  chk(r === 4'b0x10, "a << n");
    r = sa >>> 1; chk(r === 4'b11x0, "sa >>> 1");
    r = sa <<< 1; chk(r === 4'bx010, "sa <<< 1");
    r = sz >>> 2; chk(r === 4'bzzz0, "sz >>> 2");
    r = sz >> 1;  chk(r === 4'b0z00, "sz >> 1");
    chk((a << 1) === 4'b0x10, "a << 1 expr");
    chk((a >> n) === 4'b010x, "a >> n expr");
    chk((4'b10x1 << 1) === 4'b0x10, "literal << 1");
    chk((a << 4'bx) === 4'bxxxx, "x shift amount");
    chk((a >> 1'bz) === 4'bxxxx, "z shift amount");
    chk((sa >>> n) === 4'sb11x0, "sa >>> n");
    chk((a << 4) === 4'b0000, "a << 4");
    chk((a >> 9) === 4'b0000, "a >> 9");
    chk((a << sh) === 4'bxxxx, "a << sh with x");
    chk((b8 << 8) === 8'b0, "b8 << 8");
    chk((b8 >> 8) === 8'b0, "b8 >> 8");
    chk((b8 << big) === 8'b0, "b8 << big");
    chk(($signed(b8) >>> big) === 8'sb11111111, "signed >>> big");
    chk(($signed(b8) >>> 3) === 8'sb11111110, "signed >>> 3");
    wr = w << 1; chk(wr === 100'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0000000000000000000000000000000000000000000000000000000001z00, "w << 1");
    wr = w >> 1; chk(wr === 100'b0xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0000000000000000000000000000000000000000000000000000000001z, "w >> 1");
    wr = w << m; chk(wr === 100'b0000000000000000000000000001z00000000000000000000000000000000000000000000000000000000000000000000000, "w << m");
    wr = w >> m; chk(wr === 100'b0000000000000000000000000000000000000000000000000000000000000000000000xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx, "w >> m");
    wr = sw >>> 3; chk(wr === 100'bxxxx000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000, "sw >>> 3");
    wr = sw >>> m; chk(wr === 100'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx00000000000000000000000000000, "sw >>> m");
    wr = w << sh; chk(wr === 100'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx, "w << sh");
    wr = w << 100; chk(wr === 100'b0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000, "w << 100");
    wr = 100'h1 << 99; chk(wr === {1'b1, 99'b0}, "1 << 99");
    wr = 100'h1 << 64; chk(wr === 100'h0_0000_0001_0000_0000_0000_0000, "1 << 64");
    if (err == 0) $display("PASSED");
  end
endmodule
