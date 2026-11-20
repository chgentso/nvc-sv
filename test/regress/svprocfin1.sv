module svprocfin1;
  process p1, p2;
  int done2 = 0;

  initial begin
    p1 = process::self();
  end

  initial begin
    p2 = process::self();
    #5;
    done2 = 1;
  end

  initial begin
    #1;
    if (p1.status() != process::FINISHED) $fatal(1, "p1 not FINISHED");
    if (p2.status() != process::WAITING)  $fatal(1, "p2 not WAITING");
    p2.await();
    if (!done2) $fatal(1, "p2 did not finish before await returned");
    if (p2.status() != process::FINISHED) $fatal(1, "p2 not FINISHED after await");
    $display("PASSED");
    $finish;
  end
endmodule
