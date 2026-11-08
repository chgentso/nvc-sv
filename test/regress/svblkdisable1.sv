// disable of an enclosing named block or named fork that contains forked processes (self-checking).
// Expected values: IEEE 1800-2017 9.6.2 (disable terminates the named block/fork and all processes started
// inside it; execution continues after the block), 9.3.2 (join_any resumes when the first branch completes;
// a branch that only spawns a join_none child completes immediately), 9.4.1 (#delay).
// Reference model (log of "<time> <text>"): 4 disabling; 4 after blk (the #10 branch is killed, never prints);
// 24 done; at 24 the join_any returns at once (branch 3 only spawns a child): 24 b2 body; 44 end; the named
// fork f3 is disabled at 48 by its first branch: 48 after f3 (the #10 branch never prints).
// Left out: `disable b2` fires at 28, after block b2 has already completed (at 24).  Whether this late disable
// also terminates the still-running children started inside b2 (messages "not reached 2" at 34 and
// "not reached 3" at 36) is not settled by 9.6.2, so those two messages are tolerated either way.
`timescale 1ns/1ns
module svblkdisable1;
  string log [$];
  int errs = 0;
  task automatic note(string s);
    log.push_back($sformatf("%0d %s", $time, s));
  endtask
  initial begin
    begin : blk
      fork
        begin #4; note("disabling"); disable blk; end
        begin #10 note("not reached"); end
      join
    end
    note("after blk");
    #20 note("done");
    begin : b2
      fork
        begin #4 disable b2; end
        begin #10 note("not reached 2"); end
        begin fork #12 note("not reached 3"); join_none end
      join_any
      note("b2 body");
    end
    #20 note("end");
    fork : f3
      begin #4 disable f3; end
      begin #10 note("not reached 4"); end
    join
    note("after f3");
    #20;
    begin
      automatic string exp[$] = '{"4 disabling", "4 after blk", "24 done", "24 b2 body", "44 end", "48 after f3"};
      string got[$];
      foreach (log[i])
        if (!(log[i] == "34 not reached 2" || log[i] == "36 not reached 3")) got.push_back(log[i]);
      if (got.size() != exp.size()) begin
        errs++; $error("log has %0d entries, expected %0d", got.size(), exp.size());
        foreach (log[i]) $display("  log: %s", log[i]);
      end else
        foreach (exp[i]) if (got[i] != exp[i]) begin errs++; $error("entry %0d: '%s', expected '%s'", i, got[i], exp[i]); end
      if (errs == 0) $display("PASSED");
    end
  end
endmodule
