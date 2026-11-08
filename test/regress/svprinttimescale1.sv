// $printtimescale in modules with different `timescale directives; $timeunit/$timeprecision report
// the unit and precision (powers of ten of a second) in effect for each module.
// The text printed by $printtimescale itself is not compared (only that it runs in every scope).
// Expected values: IEEE 1800-2017 20.4 ($printtimescale, $timeunit, $timeprecision), 22.7 (`timescale)
`timescale 1ns/1ps
package pk;
  int n = 0;
  int errs = 0;
endpackage
`timescale 1 ns / 1 ns
module m0(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -9 || $timeprecision != -9) begin pk::errs++; $error("m0: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 10 ns / 1 ns
module m1(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -8 || $timeprecision != -9) begin pk::errs++; $error("m1: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 100 ns / 10 ns
module m2(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -7 || $timeprecision != -8) begin pk::errs++; $error("m2: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 1 us / 1 us
module m3(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -6 || $timeprecision != -6) begin pk::errs++; $error("m3: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 10 us / 10 us
module m4(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -5 || $timeprecision != -5) begin pk::errs++; $error("m4: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 100 us / 100 us
module m5(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -4 || $timeprecision != -4) begin pk::errs++; $error("m5: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 1 ms / 1 ms
module m6(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -3 || $timeprecision != -3) begin pk::errs++; $error("m6: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 10 ms / 1 ms
module m7(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -2 || $timeprecision != -3) begin pk::errs++; $error("m7: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 100 ms / 1 ms
module m8(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -1 || $timeprecision != -3) begin pk::errs++; $error("m8: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 1 s / 1 s
module m9(); initial begin $printtimescale; pk::n++;
  if ($timeunit != 0 || $timeprecision != 0) begin pk::errs++; $error("m9: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 10 s / 1 s
module m10(); initial begin $printtimescale; pk::n++;
  if ($timeunit != 1 || $timeprecision != 0) begin pk::errs++; $error("m10: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 1 ps / 1 ps
module m12(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -12 || $timeprecision != -12) begin pk::errs++; $error("m12: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 10 ps / 1 ps
module m13(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -11 || $timeprecision != -12) begin pk::errs++; $error("m13: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 100 ps / 10 ps
module m14(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -10 || $timeprecision != -11) begin pk::errs++; $error("m14: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 1 fs / 1 fs
module m15(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -15 || $timeprecision != -15) begin pk::errs++; $error("m15: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 10 fs / 1 fs
module m16(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -14 || $timeprecision != -15) begin pk::errs++; $error("m16: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 100 fs / 10 fs
module m17(); initial begin $printtimescale; pk::n++;
  if ($timeunit != -13 || $timeprecision != -14) begin pk::errs++; $error("m17: unit %0d precision %0d", $timeunit, $timeprecision); end
end endmodule
`timescale 1ns/1ps
module svprinttimescale1(); m0 i0();m1 i1();m2 i2();m3 i3();m4 i4();m5 i5();m6 i6();m7 i7();m8 i8();m9 i9();m10 i10();m12 i12();m13 i13();m14 i14();m15 i15();m16 i16();m17 i17();
  initial begin
    #1;
    if ($timeunit != -9 || $timeprecision != -12) begin pk::errs++; $error("top: unit %0d precision %0d", $timeunit, $timeprecision); end
    if (pk::n != 17) begin pk::errs++; $error("only %0d modules ran", pk::n); end
    if (pk::errs == 0) $display("PASSED");
  end
endmodule
