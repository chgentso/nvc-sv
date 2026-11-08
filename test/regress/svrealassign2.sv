// Compound assignment operators on a real variable

module svrealassign2;
  real phase = 7.0;
  real r;
  initial begin
    phase -= 2.0 * 3.14159265358;
    if (phase < 0.71681 || phase > 0.71682) $fatal(1, "FAILED -= %f", phase);
    phase += 1.0;
    phase *= 2.0;
    phase /= 4.0;
    if (phase < 0.85840 || phase > 0.85841) $fatal(1, "FAILED %f", phase);
    r = 10.0;
    r -= 3;
    r *= 1.5;
    if (r != 10.5) $fatal(1, "FAILED r=%f", r);
    $display("PASSED");
  end
endmodule
