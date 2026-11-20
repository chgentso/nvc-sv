module svwaitproc1;
  process p;
  int ran = 0;

  initial begin
    wait (p);
    p.kill();
    p.await();
    if (p.status() != process::KILLED) $fatal(1, "expected KILLED");
    #20;
    if (ran != 1) $fatal(1, "killed process ran again");
    $display("PASSED");
    $finish;
  end

  initial begin
    #2;
    p = process::self();
    ran++;
    #5;
    ran++;
  end
endmodule
