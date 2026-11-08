// for loops with empty initialization, condition or step parts
// Expected values: IEEE 1800-2017 12.7.1 (for loop: an empty condition is true, empty init/step are allowed,
// comma separated init and step lists), 12.8 (break). n counts i=0..4 (5 passes), m counts up to 7, f(50) is the
// first i with i*i >= 50 (8), and i,j = 0,10 ... meet (i >= j) at 5,5.
module svforempty1;
  int n, m;
  int errs = 0;
  function automatic int f(int lim);
    int r = 0;
    for (int i = 0; ; i++) begin
      if (i * i >= lim) begin r = i; break; end
    end
    return r;
  endfunction
  task automatic chk(bit ok, string msg);
    if (!ok) begin $error("%s", msg); errs++; end
  endtask
  initial begin
    automatic int fi = -1, fj = -1;
    for (int i = 0; ; i++) begin n++; if (i == 4) break; end
    $display("n=%0d", n);
    chk(n == 5, "n must be 5");
    for (;;) begin m++; if (m > 6) break; end
    $display("m=%0d", m);
    chk(m == 7, "m must be 7");
    $display("%0d", f(50));
    chk(f(50) == 8, "f(50) must be 8");
    for (int i = 0, j = 10; ; i++, j--) if (i >= j) begin fi = i; fj = j; $display("%0d %0d", i, j); break; end
    chk(fi == 5 && fj == 5, "i,j must meet at 5 5");
    if (errs == 0) $display("PASSED");
  end
endmodule
