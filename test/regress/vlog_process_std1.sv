module vlog_process_std1;
  std::process proc;
  std::semaphore sem;
  std::mailbox #(int) mbx;
  int child_ran = 0;

  function process cur_proc();
    return std::process::self();
  endfunction

  function void touch_self();
    void'(process::self());
    void'(std::process::self());
  endfunction

  task spawn_and_kill();
    fork
      #1 proc.kill();
      #3 child_ran = 1;
    join_none
  endtask

  initial begin
    int x, y;
    std::process::state st;

    touch_self();
    sem = new(1);
    mbx = new();
    mbx.put(42);
    mbx.get(x);
    if (x != 42) $fatal(1, "mailbox mismatch");

    process::self().srandom(17);
    x = $urandom();
    cur_proc().srandom(17);
    y = $urandom();
    if (x != y) $fatal(1, "srandom mismatch: %0d vs %0d", x, y);

    proc = std::process::self();
    st = proc.status();
    if (st != std::process::RUNNING) $fatal(1, "bad status %0d", st);

    fork
      begin
        proc = std::process::self();
        spawn_and_kill();
        #10 child_ran = 2;
      end
    join_none

    #5;
    if (child_ran != 0) $fatal(1, "descendant was not killed: %0d", child_ran);

    $display("PASSED");
    $finish;
  end
endmodule
