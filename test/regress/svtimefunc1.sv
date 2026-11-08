// IEEE 1800-2017 20.4.2: $timeunit and $timeprecision return the time unit
// and precision of the module they are called from as the power of ten of
// the value in seconds (1ns gives -9, 100ps gives -10), see also 3.14.2.
`timescale 10ns/100ps
module svtimefunc1_sub(output int u, output int p);
  initial begin
    u = $timeunit;
    p = $timeprecision;
  end
endmodule

`timescale 1us/1fs
module svtimefunc1;
  int u, p, errs;
  svtimefunc1_sub s(u, p);
  initial begin
    #1;
    if ($timeunit != -6) begin $error("$timeunit %0d, expected -6", $timeunit); errs++; end
    if ($timeprecision != -15) begin $error("$timeprecision %0d, expected -15", $timeprecision); errs++; end
    if (u != -8) begin $error("$timeunit of sub %0d, expected -8", u); errs++; end
    if (p != -10) begin $error("$timeprecision of sub %0d, expected -10", p); errs++; end
    if ($timeunit - 1 != -7) begin $error("$timeunit is not a signed integer"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
