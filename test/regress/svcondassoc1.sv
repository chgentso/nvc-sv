// Conditional operator with associative array tests; only the selected operand is evaluated
// Expected values: IEEE 1800-2017 11.4.11 (conditional operator evaluates only the
// selected branch), 7.9.3 (exists), 7.8 (associative arrays), 6.21 (initializers in begin-end)
module svcondassoc1;
  int a[int];
  int cnt = 0;
  int errs = 0;
  function int f(); cnt++; return 5; endfunction
  initial begin
    automatic int x = a.exists(3) ? a[3] : 0;
    automatic int y = (cnt == 0) ? 1 : f();
    if (x != 0 || y != 1 || cnt != 0) begin $error("first: %0d %0d %0d", x, y, cnt); errs++; end
    if (a.num() != 0) begin $error("array was extended by the read"); errs++; end
    x = (!a.exists(4) || a[4] == 1) ? 7 : 8;
    if (x != 7) begin $error("second: %0d", x); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
