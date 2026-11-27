// Test %t format specifier in $sscanf before and after $timeformat

`timescale 1ns / 100ps

module svsscanft1;
  real r;
  integer rc;
  time t;

  initial begin
    rc = $sscanf("8125", "%t", r);
    if (rc !== 1) $fatal(1, "rc1=%0d exp 1", rc);
    t = r;
    if (t !== 813) $fatal(1, "t1=%0d exp 813", t);

    $timeformat(-3, 2, "ms", 10);
    rc = $sscanf("8.125", "%t", r);
    if (rc !== 1) $fatal(1, "rc2=%0d exp 1", rc);
    t = r;
    if (t !== 8125000) $fatal(1, "t2=%0d exp 8125000", t);

    $display("PASSED");
    $finish;
  end
endmodule
