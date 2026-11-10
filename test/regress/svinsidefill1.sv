// Self-checking. Expected values: IEEE 1800-2017 11.4.13 (a value is inside
// the set if it equals one of its members; the members are compared with the
// width of the expression), 5.7.1 (an unbased unsized literal '0 or '1 fills
// every bit of the width of its context, here the other operand).
module svinsidefill1;
  logic [7:0] a = 8'hff, b = 8'h00, c = 8'h0f;
  logic [3:0] w = 4'hf;
  int errs = 0;
  initial begin
    if (!(a inside {'0, '1})) begin errs++; $error("a"); end
    if (!(b inside {'0, '1})) begin errs++; $error("b"); end
    if (c inside {'0, '1}) begin errs++; $error("c"); end
    if (!(w inside {'1})) begin errs++; $error("w"); end
    if (c inside {'1}) begin errs++; $error("c all ones"); end
    if (!(a[7:4] inside {'0, '1})) begin errs++; $error("slice"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
