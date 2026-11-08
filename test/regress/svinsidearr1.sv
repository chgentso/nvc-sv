// Expected values: IEEE 1800-2017 11.4.13 (set membership; array operands in the set are
// traversed element by element)
module svinsidearr1;
  int impl[] = '{3, 5, 9, 200};
  int q[$] = {1, 2};
  int errs = 0;
  function bit f(int x); return x inside {impl}; endfunction
  function bit g(int x); return x inside {q, 10}; endfunction
  initial begin
    int v;
    v = 5;
    if (!(v inside {impl})) begin errs++; $error("5 inside impl"); end
    if (f(3) !== 1'b1) begin errs++; $error("f(3)"); end
    if (f(4) !== 1'b0) begin errs++; $error("f(4)"); end
    if (g(2) !== 1'b1) begin errs++; $error("g(2)"); end
    if (g(10) !== 1'b1) begin errs++; $error("g(10)"); end
    if (g(3) !== 1'b0) begin errs++; $error("g(3)"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
