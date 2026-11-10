// Self-checking. Expected values: IEEE 1800-2017 11.4.14.1 (the slice_size of
// a streaming concatenation is a simple type, whose width is the size of the
// slice, or a constant expression; the << operator reverses the order of the
// slices from the right and bit, a one bit type, gives a bit reversal).
module svstreamtype1;
  typedef logic [3:0] nib_t;
  logic [7:0] m = 8'b1010_0011, r1, r2, r3, r4;
  int errs = 0;
  initial begin
    r1 = {<< bit {m}};
    r2 = {<< nib_t {m}};
    r3 = {<< byte {m}};
    r4 = {<< logic {m}};
    if (r1 !== 8'b1100_0101) begin errs++; $error("bit %b", r1); end
    if (r2 !== 8'b0011_1010) begin errs++; $error("nib_t %b", r2); end
    if (r3 !== 8'b1010_0011) begin errs++; $error("byte %b", r3); end
    if (r4 !== 8'b1100_0101) begin errs++; $error("logic %b", r4); end
    if (errs == 0) $display("PASSED");
  end
endmodule
