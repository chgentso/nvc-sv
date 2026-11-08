// Compound assignment operators: operand sizing, signedness and truncation
// Expected values: IEEE 1800-2017 11.4.1 (a op= b behaves as a = a op (b)), 11.6 (expression sizing),
// 11.8 (signedness), 11.4.10 (shifts), 6.16 (string indexing yields a byte)
module svopassign1;
  typedef int unsigned uint32_t;
  string s = "rule";
  uint32_t d[];
  uint32_t x;
  real r = 1.5;
  int i = 3;
  bit [3:0] n = 4'hf;
  byte b = 8'sd100;
  logic [7:0] l = 8'h42;
  logic [15:0] w = 16'h1234;
  int errs = 0;
  initial begin
    d = new[1];
    d[0] = 0;
    for (int k = 0; k < s.len(); k++)
      d[k/4] |= s[k] << (8 * (k % 4));
    if (d[0] !== 32'h656c7572) begin $error("string pack %h", d[0]); errs++; end
    b = 8'h41;
    x = 0; x |= b << 8;
    if (x !== 32'h00004100) begin $error("|= %h", x); errs++; end
    x = 0; x += l << 16;
    if (x !== 32'h00420000) begin $error("+= %h", x); errs++; end
    x = 0; x ^= l << 12;
    if (x !== 32'h00042000) begin $error("^= %h", x); errs++; end
    x = 32'hffffffff; x &= ~(l << 4);
    if (x !== 32'hfffffbdf) begin $error("&= %h", x); errs++; end
    x = 0; x -= l << 20;
    if (x !== 32'hfbe00000) begin $error("-= %h", x); errs++; end
    x = 0; x |= {l} << 20;
    if (x !== 32'h04200000) begin $error("|= concat %h", x); errs++; end
    r += 1; r *= 2;
    if (r != 5.0) begin $error("real %f", r); errs++; end
    i -= 8'hff;
    if (i !== -252) begin $error("i -= 8'hff gave %0d", i); errs++; end
    n += 8'h11;
    if (n !== 4'h0) begin $error("n += 8'h11 gave %h", n); errs++; end
    b = 8'sd100; b += 8'sd100;
    if (b !== -8'sd56) begin $error("byte overflow %0d", b); errs++; end
    w >>= 4'd8;
    if (w !== 16'h0012) begin $error(">>= %h", w); errs++; end
    w <<= 8;
    if (w !== 16'h1200) begin $error("<<= %h", w); errs++; end
    i = 7; i %= 4;
    if (i !== 3) begin $error("%%= %0d", i); errs++; end
    i = 8; i /= 3;
    if (i !== 2) begin $error("/= %0d", i); errs++; end
    i <<<= 2; i >>>= 1;
    if (i !== 4) begin $error("<<<= >>>= %0d", i); errs++; end
    n = 4'h3; n |= 8'hf0 >> 4;
    if (n !== 4'hf) begin $error("n |= (8'hf0>>4) gave %h", n); errs++; end
    b = -8; b >>>= 1;
    if (b !== -8'sd4) begin $error("signed >>>= %0d", b); errs++; end
    w = 16'hffff; w &= 32'h0001_ff00;
    if (w !== 16'hff00) begin $error("w &= wide %h", w); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
