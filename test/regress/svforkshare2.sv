// Self-checking. IEEE 1800-2017 9.3.2: a process forked with join_none sees
// the automatic variables of the enclosing scope by reference, so an
// assignment by the parent after the fork is seen by a child that only reads
// the variable.  A variable declared in the fork itself and initialised from a
// loop variable (automatic int j = i) is a copy made when the fork starts.
module svforkshare2;
  int errs = 0;
  int seen[3];
  task automatic spawn();
    bit flag = 0;
    fork
      begin
        wait(flag);
        if ($time != 10) begin errs++; $error("woke at %0t", $time); end
      end
    join_none
    #10 flag = 1;
    #5;
  endtask
  task automatic loop();
    for (int i = 0; i < 3; i++) begin
      fork
        automatic int j = i;
        begin #1 seen[j] = j + 1; end
      join_none
    end
  endtask
  initial begin
    spawn();
    loop();
    #5;
    for (int k = 0; k < 3; k++)
      if (seen[k] != k + 1) begin errs++; $error("seen[%0d] = %0d", k, seen[k]); end
    if (errs == 0) $display("PASSED");
  end
endmodule
