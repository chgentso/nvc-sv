// IEEE 1800-2017 9.7: process::suspend() stops a process.  resume() of a
// process suspended while blocked waits again for its delay or event: if the
// delay has already elapsed it continues in the current time step, while an
// event control is re-sensitized, so an event that happened during the
// suspension is not remembered and the next one wakes the process.  A suspended process reports SUSPENDED.
// A process suspending itself stops at once.  Killing a suspended process
// makes it KILLED and it never runs again; await() returns when the
// awaited process finishes or is killed.  resume() of a process that is not
// suspended has no effect.
module svprocsusp1;
  process p1, p2, p3, p4;
  event ev;
  int cnt = 0;
  int stage2 = 0;
  int ran3 = 0;
  time woke1, woke4;

  initial begin
    fork
      begin                       // p1: waits on a delay
        p1 = process::self();
        #10;
        woke1 = $time;
      end
      begin                       // p2: counts each 1 time unit
        p2 = process::self();
        forever #1 cnt++;
      end
      begin                       // p3: suspends itself
        p3 = process::self();
        stage2 = 1;
        p3.suspend();
        stage2 = 2;
      end
      begin                       // p4: waits on an event
        p4 = process::self();
        @(ev) woke4 = $time;
        ran3 = 1;
        #1000 ran3 = 2;
      end
    join_none

    #1;
    if (p3.status() != process::SUSPENDED || stage2 != 1)
      $fatal(1, "self suspend: status %0d stage %0d", p3.status(),
             stage2);
    if (p1.status() != process::WAITING)
      $fatal(1, "p1 status %0d", p1.status());

    // Suspend a waiting process past its wake-up time
    p1.suspend();
    if (p1.status() != process::SUSPENDED)
      $fatal(1, "p1 after suspend: %0d", p1.status());
    #20;                          // Now 21: the #10 elapsed meanwhile
    if (woke1 != 0) $fatal(1, "suspended p1 ran at %0d", woke1);
    p1.resume();
    #0;
    if (woke1 != 21) $fatal(1, "p1 resumed at %0d, expected 21", woke1);
    p1.await();
    if (p1.status() != process::FINISHED)
      $fatal(1, "p1 final status %0d", p1.status());

    // Suspend a running counter and check it stops counting
    p2.suspend();
    begin
      automatic int c0 = cnt;
      #5;
      if (cnt != c0) $fatal(1, "suspended p2 counted %0d -> %0d", c0, cnt);
    end
    p2.resume();
    p2.resume();                  // No effect when not suspended
    #5;
    if (p2.status() == process::SUSPENDED)
      $fatal(1, "p2 still suspended");
    p2.kill();

    // Event while suspended: the process resumes only on resume()
    p4.suspend();
    #1 -> ev;
    #3;
    if (woke4 != 0) $fatal(1, "suspended p4 ran at %0d", woke4);
    p4.resume();
    #0;
    if (woke4 != 0) $fatal(1, "p4 woke on an event before resume()");
    #1 -> ev;
    #0;
    if (woke4 != $time) $fatal(1, "p4 woke at %0d, now %0d", woke4, $time);

    // Kill of a suspended process
    p4.suspend();
    p4.kill();
    if (p4.status() != process::KILLED)
      $fatal(1, "p4 after kill: %0d", p4.status());
    p4.await();                   // Returns at once: already killed
    #2000;
    if (ran3 != 1) $fatal(1, "killed p4 ran on: %0d", ran3);

    // A self-suspended process resumes after its suspend() call
    p3.resume();
    p3.await();
    if (stage2 != 2 || p3.status() != process::FINISHED)
      $fatal(1, "p3 resume: stage %0d status %0d", stage2,
             p3.status());

    $display("PASSED");
  end
endmodule
