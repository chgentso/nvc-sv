// Self-checking. Expected values: IEEE 1800-2017 18.5.4 (a dist constraint
// gives a weight to each value or range, which may use $ for the smallest or
// largest value of the variable; weights are relative: 0 :/ 20 and [1:$] :/ 80
// make about one value in five zero), 18.5.3 (inside ranges use $ in the same
// way) and 18.7 (in-line constraints with std::randomize).
module svdistdollar1;
  int unsigned glen;
  bit [3:0] sm;
  int zeros = 0, big = 0, errs = 0, max_sm = 0;
  initial begin
    repeat (400) begin
      if (!std::randomize(glen) with { glen dist { 0 :/ 20, [1:$] :/ 80 }; }) errs++;
      if (glen == 0) zeros++; else big++;
      if (!std::randomize(sm) with { sm dist { 0 :/ 1, [1:$] :/ 3 }; }) errs++;
      if (sm > max_sm) max_sm = sm;
    end
    if (errs != 0) begin $error("randomize failed %0d times", errs); end
    else if (zeros < 40 || zeros > 130 || big == 0) $error("zeros = %0d of 400", zeros);
    else if (max_sm < 12) $error("max of the 4 bit value was %0d", max_sm);
    else $display("PASSED");
  end
endmodule
