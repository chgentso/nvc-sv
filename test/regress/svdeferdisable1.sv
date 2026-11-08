// Deferred immediate assertions: disable statements and process resumption
// as flush points.
// IEEE 1800-2017 16.4.4: disabling the outermost scope of a procedure from
// within flushes its deferred assertion report queue; disabling an inner named block cancels
// the pending reports of the deferred assertions within that block and not
// those of the enclosing block.  16.4.2: the queue of
// a process is flushed when the process resumes after suspending (after an
// event control, a #0 delay, or a join), so only the evaluations made after
// the last resumption of a process in a time step report.  Each process,
// including one spawned by fork, has its own queue (16.4.2), so the
// resumption of one process does not flush the reports of another.
// The action blocks are single subroutine calls (16.4); reports are compared
// as a sorted list since the order of the processes in a time step is not
// specified.
`timescale 1ns/1ns
module svdeferdisable1;
  string reps[$];
  function void rep(string id, int v);
    reps.push_back($sformatf("%0t:%s:%0d", $time, id, v));
  endfunction

  // Outermost scope disabled from within
  int td = 0;
  always @(td) begin : b0
    a0: assert #0 (td == 0) else rep("a0", td);
    if (td == 1) disable b0;
    a0b: assert #0 (td == 0) else rep("a0b", td);
  end

  // Inner named block disabled from within: its pending reports are
  // cancelled, the enclosing block's are not
  int tb = 0;
  always @(tb) begin : ob
    a2: assert #0 (tb == 0) else rep("a2", tb);
    begin : ib
      a3: assert #0 (tb == 0) else rep("a3", tb);
      disable ib;
    end
    a3b: assert #0 (tb == 0) else rep("a3b", tb);
  end

  // Processes spawned by fork
  event ev, ev2;
  int nf1 = 0;
  initial
    fork
      forever begin
        @ev;
        nf1++;
        a6: assert #0 (0) else rep("a6", nf1);
      end
      begin
        @ev;
        a7: assert #0 (0) else rep("a7", 0);
        @ev2;
      end
    join_none

  initial begin
    #15 td = 1;                 // Flushed by the disable
    #2  td = 2;                 // a0 and a0b reported
    #3  tb = 1;                 // a2 and a3b reported, a3 cancelled
    #20 -> ev;                  // a6 evaluated three times, a7 once
    #0  -> ev;
    #0  -> ev;
    a8: assert #0 (0) else rep("a8", 0);   // Reported: no resumption after
    #10;
    a9: assert #0 (0) else rep("a9", 0);   // Flushed when resuming at join
    fork #0; join
    a10: assert #0 (0) else rep("a10", 1); // Reported
  end

  initial begin
    automatic string exp[$] = '{"17:a0:2", "17:a0b:2", "20:a2:1",
                                "20:a3b:1", "40:a6:3", "40:a7:0",
                                "40:a8:0", "50:a10:1"};
    #100;
    reps.sort();
    exp.sort();
    if (reps != exp) begin
      $display("reports %p", reps);
      $display("expected %p", exp);
      $error("mismatch");
    end
    else
      $display("PASSED");
  end
endmodule
