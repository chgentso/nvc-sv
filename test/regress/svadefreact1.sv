// Action blocks of observed deferred assertions run in the Reactive region.
// IEEE 1800-2017 16.4.1: the report of an observed deferred assertion
// (assert #0) matures in the Observed region after the last evaluation in
// the Active/Inactive/NBA loop of the time step, so a glitch is never
// reported, and its pass or fail statement is scheduled in the Reactive
// region.  4.4.2.7/4.5: the Reactive region may update variables, which
// schedules more Active events in the same time step, so an action block may
// assign a module variable, and an assertion made to run again by that
// assignment reports again in the same time step.
module svadefreact1;
  logic c = 1, d = 0;
  int   errs = 0, oks = 0, glitch = 0, lerr = 0, seen = 0, errors = 0;
  time  seen_at = -1;

  always @(c) assert #0 (c) oks++; else begin errs++; c = 1; end

  always @(errs) begin
    seen++;
    seen_at = $time;
  end

  always @(d) assert #0 (d == 0) else glitch++;

  task automatic t(input int k);
    int loc = k;
    assert #0 (loc != 3) else lerr = lerr + loc;
  endtask

  task automatic check(int got, int exp, string what);
    if (got !== exp) begin
      $display("%0t: %s = %0d, expected %0d", $time, what, got, exp);
      errors++;
    end
  endtask

  initial begin
    #1 c = 0;
    #1;
    check(errs, 1, "errs");
    check(oks, 1, "oks after the action set c again");
    check(c, 1, "c");
    check(seen, 1, "seen");
    check(seen_at, 1, "time the action's assignment was seen");

    d = 1; d = 0;
    #1 d = 1;
    #0 d = 0;
    #1 check(glitch, 0, "glitch");

    t(3);
    t(4);
    #1 check(lerr, 3, "lerr");

    if (errors == 0)
      $display("PASSED");
  end
endmodule
