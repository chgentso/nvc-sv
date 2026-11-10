// Self-checking. IEEE 1800-2017 9.3.2: the processes of a fork share the
// automatic variables of the enclosing scope, so a variable which one process
// assigns is seen by a sibling that only reads it, also with join_none after
// the task which declared it has returned.
module svforkshare1;
  int errs = 0;
  task automatic spawn();
    bit flag = 0;
    fork
      begin #10 flag = 1; end
      begin
        wait(flag);
        if ($time != 10) begin errs++; $error("woke at %0t", $time); end
        #5 if (flag !== 1'b1) begin errs++; $error("flag lost"); end
      end
      begin
        @(flag);
        if ($time != 10) begin errs++; $error("event at %0t", $time); end
      end
      begin #20 if (flag !== 1'b1) begin errs++; $error("late read %b", flag); end end
    join_none
  endtask
  initial begin
    spawn();
    #30 if (errs == 0) $display("PASSED");
  end
endmodule
