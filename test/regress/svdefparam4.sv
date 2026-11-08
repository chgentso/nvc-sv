// defparam inside generate blocks, through a top-qualified name, and conflicting defparams.
// Expected values: IEEE 1800-2017 23.10.1 (defparam; with several defparams for one parameter the last in
// source text wins: g[1].l.P is set to 11 inside the loop and later to 99 at the top level), 27.4 (loop
// generate), 27.5 (conditional generate), 23.6 (hierarchical references to parameters).
module svdefparam4_leaf; parameter P = 1; time fired = 0; initial begin #P; fired = $time; end endmodule
module svdefparam4;
  generate
    genvar i;
    for (i = 0; i < 2; i = i + 1) begin : g
      svdefparam4_leaf l();
      defparam l.P = 10 + i;
    end
    if (1) begin : h
      svdefparam4_leaf l2();
      defparam l2.P = 7;
    end
  endgenerate
  svdefparam4_leaf l3();
  defparam svdefparam4.l3.P = 5;
  defparam g[1].l.P = 99;
  int errs = 0;
  task automatic chk(string what, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d, expected %0d", what, got, exp); end
  endtask
  task automatic chk2(string n, int p, time f, int e);
    chk({n, ".P"}, p, e); chk({n, " fired"}, int'(f), e);
  endtask
  initial begin
    #1000;
    chk2("l3", l3.P, l3.fired, 5);
    chk2("h.l2", h.l2.P, h.l2.fired, 7);
    chk2("g[0].l", g[0].l.P, g[0].l.fired, 10);
    chk2("g[1].l", g[1].l.P, g[1].l.fired, 99);
    if (errs == 0) $display("PASSED");
  end
endmodule
