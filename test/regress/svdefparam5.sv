// Several defparams for the same parameter: the last one in source text wins.
// Expected values: IEEE 1800-2017 23.10.1 ("In the case of multiple defparams for a single parameter, the
// parameter takes the value of the last defparam statement encountered in the source text"): g[0].l.P is set
// to 98, 10 and 97 (last: 97); g[1].l.P is set to 99 and then 11 (last: 11). 27.4 (loop generate),
// 23.6 (hierarchical references to parameters).
module svdefparam5_leaf; parameter P = 1; time fired = 0; initial begin #P; fired = $time; end endmodule
module svdefparam5;
  defparam g[1].l.P = 99;
  defparam g[0].l.P = 98;
  generate
    genvar i;
    for (i = 0; i < 2; i = i + 1) begin : g
      svdefparam5_leaf l();
      defparam l.P = 10 + i;
    end
  endgenerate
  defparam g[0].l.P = 97;
  int errs = 0;
  task automatic chk(string what, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d, expected %0d", what, got, exp); end
  endtask
  initial begin
    #1000;
    chk("g[0].l.P", g[0].l.P, 97);
    chk("g[0].l fired", int'(g[0].l.fired), 97);
    chk("g[1].l.P", g[1].l.P, 11);
    chk("g[1].l fired", int'(g[1].l.fired), 11);
    if (errs == 0) $display("PASSED");
  end
endmodule
