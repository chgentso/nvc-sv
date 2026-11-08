// Pass and fail actions of concurrent assertions, covers and expect, and the
// assertion control tasks which switch them.
// IEEE 1800-2017:
//   16.14.1  the pass statement runs when an attempt succeeds, and a vacuous
//            success (16.12.7: no match of the antecedent) is a success;
//   16.14.3  cover property runs its statement once per attempt that succeeds
//            and does not count vacuous successes;
//   16.12 / 16.15  an attempt disabled by disable iff runs neither statement;
//   16.17    expect runs its pass statement when the property succeeds,
//            vacuously or not;
//   20.12    $assertcontrol control types 1 Lock, 2 Unlock, 3 On, 4 Off (no new
//            attempts, those in progress go on), 5 Kill (also aborts the
//            attempts in progress), 6 PassOn, 7 PassOff, 8 FailOn, 9 FailOff,
//            10 NonvacuousOn, 11 VacuousOff; $asserton/$assertoff/$assertkill
//            are $assertcontrol(3/4/5, 15, 7, ...) and $assertpasson/off,
//            $assertfailon/off, $assertnonvacuouson and $assertvacuousoff are
//            $assertcontrol(6/7/8/9/10/11, 31, 7, ...), so the latter also
//            apply to expect (assertion type 16).
// The clock rises at 5, 15, 25, ... and the inputs change at the falling
// edges, so each step below is one tick.
module svavacuous1;
  logic clk = 0, a = 0, b = 0, r = 1;
  int   p1 = 0, f1 = 0, pd = 0, fd = 0, c1 = 0, c2 = 0, fm = 0, ep = 0, ef = 0;
  int   errors = 0;

  always #5 clk = ~clk;

  P1: assert property (@(posedge clk) a |-> b) p1++; else f1++;
  D1: assert property (@(posedge clk) disable iff (r) a |-> b) pd++; else fd++;
  C1: cover property (@(posedge clk) a |-> b) c1++;
  C2: cover property (@(posedge clk) a ##[1:3] b) c2++;
  M1: assert property (@(posedge clk) a |-> ##2 b) else fm++;

  task automatic check(int got, int exp, string what);
    if (got !== exp) begin
      $display("%0t: %s = %0d, expected %0d", $time, what, got, exp);
      errors++;
    end
  endtask

  task automatic step;
    @(negedge clk);
  endtask

  initial begin
    // 5: a = 0, every attempt of P1 succeeds vacuously; D1 is disabled
    step;
    check(p1, 1, "p1 vacuous");
    check(pd, 0, "pd disabled");
    check(fd, 0, "fd disabled");
    check(c1, 0, "c1 vacuous");
    r = 0;

    // 15: vacuous success of D1 too
    step;
    check(p1, 2, "p1 vacuous");
    check(pd, 1, "pd vacuous");
    a = 1; b = 1;

    // 25: non-vacuous success, counted by the cover
    step;
    check(p1, 3, "p1 nonvacuous");
    check(pd, 2, "pd nonvacuous");
    check(c1, 1, "c1 nonvacuous");
    a = 0;

    // 35, 45, 55: b stays high, the attempt of C2 at 25 is counted once
    repeat (3) step;
    check(p1, 6, "p1 vacuous");
    check(c1, 1, "c1 vacuous not counted");
    check(c2, 1, "c2 once per attempt");
    $assertvacuousoff;

    // 65: vacuous success runs no pass statement
    step;
    check(p1, 6, "p1 after $assertvacuousoff");
    check(pd, 5, "pd after $assertvacuousoff");
    a = 1; b = 0;

    // 75: failure
    step;
    check(f1, 1, "f1");
    check(fd, 1, "fd");
    $assertcontrol(10);
    b = 1;

    // 85: non-vacuous success
    step;
    check(p1, 7, "p1 after NonvacuousOn");
    check(c1, 2, "c1 nonvacuous");
    a = 0;

    // 95: still no vacuous pass statements
    step;
    check(p1, 7, "p1 vacuous after NonvacuousOn");
    $assertpassoff;
    a = 1;

    // 105: no pass statements at all, not even the cover's
    step;
    check(p1, 7, "p1 after $assertpassoff");
    check(c1, 2, "c1 after $assertpassoff");
    $assertpasson;
    a = 0;

    // 115: PassOn enables the vacuous pass statements again
    step;
    check(p1, 8, "p1 after $assertpasson");
    $assertfailoff;
    a = 1; b = 0;

    // 125: failure without the fail statement; an attempt of M1 starts
    step;
    check(f1, 1, "f1 after $assertfailoff");
    $assertfailon;
    $assertoff(0, M1);
    a = 0;

    // 135: M1 starts no attempt; 145: the attempt from 125 still fails
    repeat (2) step;
    check(fm, 1, "fm after $assertoff");
    $asserton(0, M1);
    a = 1;

    // 155: an attempt of M1 starts and is then killed
    step;
    $assertkill(0, M1);
    a = 0;
    repeat (2) step;
    check(fm, 1, "fm after $assertkill");
    $asserton(0, M1);

    // Lock: P1 ignores $assertpassoff until unlocked
    $assertcontrol(1, 15, 7, 0, P1);
    $assertpassoff;
    step;
    check(p1, 13, "p1 locked");
    $assertcontrol(2, 15, 7, 0, P1);
    $assertpassoff;
    step;
    check(p1, 13, "p1 unlocked");
    $assertpasson;

    // expect: a vacuous success runs the pass statement and ends the wait
    expect (@(posedge clk) a |-> ##1 b) ep++; else ef++;
    check(ep, 1, "ep vacuous");
    check(ef, 0, "ef vacuous");
    $assertvacuousoff;
    expect (@(posedge clk) a |-> ##1 b) ep++; else ef++;
    check(ep, 1, "ep after $assertvacuousoff");
    check(ef, 0, "ef after $assertvacuousoff");
    $assertpasson;

    if (errors == 0)
      $display("PASSED");
    $finish;
  end
endmodule
