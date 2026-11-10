// Self-checking. Expected values: IEEE 1800-2017 6.19 (an enumeration constant
// of a four-state base type may be given a value containing x or z), 5.7.1
// (an unbased unsized literal '0 '1 'x 'z fills every bit of the context:
// here the width of the base type of the enumeration).
module svenumfill1;
  typedef enum logic [2:0] {A = 3'd0, B = 3'd1, U = 'z, X = 'x} e_t;
  typedef enum logic [7:0] {Z8 = 'z, O8 = '1} w_t;
  e_t e;
  w_t w;
  int errs = 0;
  initial begin
    e = U;
    if (e !== 3'bzzz) begin errs++; $error("U = %b", e); end
    e = X;
    if (e !== 3'bxxx) begin errs++; $error("X = %b", e); end
    w = Z8;
    if (w !== 8'bzzzzzzzz) begin errs++; $error("Z8 = %b", w); end
    w = O8;
    if (w !== 8'hff) begin errs++; $error("O8 = %b", w); end
    if (errs == 0) $display("PASSED");
  end
endmodule
