// foreach over arrays whose bounds depend on a parameter, in several instances
// Expected values: IEEE 1800-2017 12.7.3 (foreach: loop variables run over each dimension from its left bound
// to its right bound, so a[N-1:0] visits N-1 down to 0 and g[0:N-1], h[N] visit 0 up to N-1; the first
// dimension of d[N-1:0][2] is the outer loop), 7.4.2 (array declarations, [N] means [0:N-1]), 6.20.2 (parameters).
class cc;
  int v;
endclass
module sub #(parameter int N = 3);
  cc a[N-1:0];
  int g[0:N-1];
  int h[N];
  int d[N-1:0][2];
  string res;
  initial begin
    res = $sformatf("N=%0d a:", N);
    foreach (a[i]) begin res = {res, $sformatf(" %0d", i)}; a[i] = new; end
    res = {res, " g:"};
    foreach (g[i]) res = {res, $sformatf(" %0d", i)};
    res = {res, " h:"};
    foreach (h[i]) res = {res, $sformatf(" %0d", i)};
    res = {res, " d:"};
    foreach (d[i, j]) res = {res, $sformatf(" %0d%0d", i, j)};
    $display("%s", res);
  end
endmodule
module svforeachdyn1;
  sub #(.N(1)) s1();
  sub #(.N(4)) s4();
  sub s3();
  int errs = 0;
  task automatic eq(string got, string exp, string m);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", m, got, exp); errs++; end
  endtask
  initial begin
    #1;
    eq(s1.res, "N=1 a: 0 g: 0 h: 0 d: 00 01", "N=1");
    eq(s4.res, "N=4 a: 3 2 1 0 g: 0 1 2 3 h: 0 1 2 3 d: 30 31 20 21 10 11 00 01", "N=4");
    eq(s3.res, "N=3 a: 2 1 0 g: 0 1 2 h: 0 1 2 d: 20 21 10 11 00 01", "N=3");
    if (errs == 0) $display("PASSED");
  end
endmodule
