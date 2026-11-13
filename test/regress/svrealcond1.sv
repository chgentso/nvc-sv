// Self-checking. IEEE 1800-2017 11.4.11: a conditional expression with a real
// operand is real, so it compares and converts as a real value where there
// is no assignment context.
module svrealcond1;
  real r;
  initial begin
    if ((0.0 ? 2.0 : 1.1) != 1.1) $error("FAIL a");
    if ((1.5 ? 2.0 : 1.1) != 2.0) $error("FAIL b");
    r = 1.5;
    if ($rtoi((r - 1.5) ? 10.0 : 20.0) != 20) $error("FAIL c");
    if ($rtoi((r - 1.0) ? 10.0 : 20.0) != 10) $error("FAIL d");
    $display("PASSED");
  end
endmodule
