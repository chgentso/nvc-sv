let in_range(x, lo, hi) = ((x) >= (lo)) && ((x) <= (hi));
let max2(a, b) = (a > b) ? a : b;
module vlog_let_unit1;
  vlog_let_unit1_b b();
  initial $display("%b %b %0d", in_range(5, 1, 9), in_range(0, 1, 9), max2(3, 7));
endmodule
let three = 3;
module vlog_let_unit1_b;
  initial #1 $display("%0d", three * max2(1, 2));
endmodule
