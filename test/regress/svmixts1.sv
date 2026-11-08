// IEEE 1800-2017 3.14.2.3: a design element without a `timescale or
// timeunit takes the default time unit of the tool.  When other design
// elements have a time unit this is likely a mistake, so nvc warns and
// names both.
`timescale 1ns/1ps
module svmixts1;
  svmixts1_sub u();
  initial begin
    #2;
    if (u.t == 1000) $display("PASSED");
    else $display("FAILED %0d", u.t);
  end
endmodule
`resetall
module svmixts1_sub;
  int t = 0;
  initial #1 t = 1000;
endmodule
