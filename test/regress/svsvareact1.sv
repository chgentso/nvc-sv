// Expected values: IEEE 1800-2017 16.4 (assertion evaluation: expressions are evaluated in
// the Observed region of the time step and action blocks run in the Reactive region, so a
// change made by an action block is not seen by the other assertions of the same time step),
// 16.14.1 (pass/fail action), 16.5.1 (sampling at the clock tick), 4.4 (scheduling regions).
// The order of action blocks of different assertions in one time step is not specified, so
// the events of each time step are compared as a set.
`timescale 1ns/1ns
module svsvareact1;
  bit clk;
  int x = 1;
  logic a = 1;
  int cnt[string];
  initial forever #5 clk = ~clk;
  function int getx(); return x; endfunction
  function void rec(string s);
    automatic string key = $sformatf("%0d %s", $time, s);
    cnt[key] = (cnt.exists(key) ? cnt[key] : 0) + 1;
  endfunction

  // The failing assertion changes x in its action block but the
  // assertions in the same time step have been evaluated before
  a1: assert property (@(posedge clk) a |-> 0)
    else begin rec("a1 fail"); x = 0; end
  a2: assert property (@(posedge clk) getx() == 1)
    else rec($sformatf("a2 fail x=%0d", x));
  a4: assert property (@(posedge clk) a |-> getx() == 1)
    begin rec("a4 pass"); end
    else rec("a4 fail");
  initial begin
    automatic string ex[$] = '{ "5 a1 fail", "5 a4 pass",
                      "15 a1 fail", "15 a2 fail x=0", "15 a4 fail",
                      "25 a1 fail", "25 a2 fail x=0", "25 a4 fail",
                      "35 a1 fail", "35 a2 fail x=0", "35 a4 fail" };
    automatic int errs = 0;
    #42;
    if (x !== 0) begin $error("x = %0d at the end, expected 0", x); errs++; end
    foreach (ex[i]) if (!cnt.exists(ex[i]) || cnt[ex[i]] != 1) begin $error("missing event '%s'", ex[i]); errs++; end
    if (cnt.num() != ex.size()) begin $error("%0d distinct events, expected %0d", cnt.num(), ex.size()); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
