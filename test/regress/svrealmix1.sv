// Self-checking. IEEE 1800-2017 11.8.2: an integral operand of an expression
// with a real operand is converted to real by its own type and value, so
// a negative signed integer stays negative.
module svrealmix1;
  real r;
  initial begin
    r = -1 * 1.5;
    if (r != -1.5) $error("FAIL a %f", r);
    r = -1 * -1.17;
    if (r != 1.17) $error("FAIL b %f", r);
    if ((1'bx >= (-1 * -1.17)) !== 1'b0) $error("FAIL c");
    if ((-2 + 0.5) != -1.5) $error("FAIL d");
    $display("PASSED");
  end
endmodule
