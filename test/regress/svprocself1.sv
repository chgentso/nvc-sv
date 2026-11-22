module svprocself1;
  initial begin
    automatic process p;
    p = process::self;
    if (p == null)
      $fatal(1, "process::self returned null");
    if (p.status() != process::RUNNING)
      $fatal(1, "expected RUNNING status");
    $display("PASSED");
    $finish;
  end
endmodule
