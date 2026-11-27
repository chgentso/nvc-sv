// Context-determined widening of signing/width/type casts and system functions
// used as operands of shift expressions (IEEE 1800-2017 6.24.1, Table 11-21)
module svcastctx2;
  logic [7:0] len;
  logic [2:0] sz;
  logic [63:0] base, res1, res2, res3;
  logic [15:0] res4;
  logic signed [7:0] sval;

  initial begin
    base = 64'h0000_0000_0000_1000;
    len  = 8'h7f;
    sz   = 3'd3;

    // unsigned'(len) is 8-bit self-determined, but widened to 64 bits by +
    // before << shifts it left by 3 bits (0x7f << 3 = 0x3f8, not 0xf8).
    res1 = base + (unsigned'(len) << sz);
    if (res1 !== 64'h0000_0000_0000_13f8)
      $fatal(1, "unsigned' shift mismatch: got %h expected 13f8", res1);

    // Same with $unsigned(), width cast, and type cast
    res2 = base + ($unsigned(len) << sz);
    if (res2 !== 64'h0000_0000_0000_13f8)
      $fatal(1, "$unsigned shift mismatch: got %h expected 13f8", res2);

    res3 = base + (8'(len) << sz);
    if (res3 !== 64'h0000_0000_0000_13f8)
      $fatal(1, "width cast shift mismatch: got %h expected 13f8", res3);

    // Signed cast widened in a signed context sign-extends before shifting
    sval = -8'sd1;
    res4 = signed'(sval) << 2;
    if (res4 !== 16'hfffc)
      $fatal(1, "signed' shift mismatch: got %h expected fffc", res4);

    // Operand inside unsigned'() remains self-determined (8-bit wrap)
    len = 8'hff;
    res1 = 64'h0 + (unsigned'(len + 8'd1) << 3);
    if (res1 !== 64'h0)
      $fatal(1, "inner self-determined mismatch: got %h expected 0", res1);

    $display("PASSED");
  end
endmodule
