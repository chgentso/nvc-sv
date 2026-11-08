// Expected values: IEEE 1800-2017 20.9 ($isunknown: 1 if any bit of the expression is x or z),
// 11.4.3 (arithmetic with x gives x), 11.4.8 (bitwise or: 1|x = 1, 0|x = x), 11.4.5 (=== is 0 or 1),
// 11.4.11 (conditional with a known condition)
module svisunk1;
  logic [3:0] a = 4'b10x1, b = 4'b1z01, c = 4'b1001;
  logic [99:0] w = {100{1'b1}};
  bit [7:0] t = 8'hff;
  int i = 5;
  logic one = 1'bz;
  int errs = 0;
  initial begin
    if ($isunknown(a) !== 1) begin errs++; $error("a"); end
    if ($isunknown(b) !== 1) begin errs++; $error("b"); end
    if ($isunknown(c) !== 0) begin errs++; $error("c"); end
    if ($isunknown(t) !== 0) begin errs++; $error("t"); end
    if ($isunknown(i) !== 0) begin errs++; $error("i"); end
    if ($isunknown(one) !== 1) begin errs++; $error("one"); end
    if ($isunknown(w) !== 0) begin errs++; $error("w"); end
    w[77] = 1'bx;
    if ($isunknown(w) !== 1) begin errs++; $error("w[77]=x"); end
    if ($isunknown(c + 4'bx) !== 1) begin errs++; $error("c+x"); end
    if ($isunknown(c | 4'bx) !== 1) begin errs++; $error("c|x"); end
    if ($isunknown(a === a) !== 0) begin errs++; $error("a===a"); end
    if (!($isunknown(a) && !$isunknown(c))) begin errs++; $error("&&"); end
    if ($isunknown(c ? a : b) !== 1) begin errs++; $error("c?a:b"); end
    if ($isunknown(!c ? a : c) !== 0) begin errs++; $error("!c?a:c"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
