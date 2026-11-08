// Time unit and precision: delays are rounded to the time precision of the module.
// The default time unit without a timescale directive is implementation-defined (3.14.2.3), so the
// timescale is declared explicitly here.
// Expected values: IEEE 1800-2017 3.14.2 (time unit/precision), 3.14.3 / 22.7 (`timescale), 20.3
// ($time, $realtime: current time in the unit of the module; delays are rounded to the precision:
// 1.5 -> 2, 0.4 -> 0, 1ps -> 0.001 ns -> 0), 20.4.2 ($printtimescale is called; its text is not checked).
`timescale 1ns/1ns
module svdefts1;
  int errs = 0;
  initial begin
    $printtimescale;
    #1.5 if ($realtime != 2.0 || $time != 2) begin errs++; $error("after #1.5: realtime=%f time=%0d, expected 2", $realtime, $time); end
    #0.4 if ($realtime != 2.0) begin errs++; $error("after #0.4: realtime=%f, expected 2", $realtime); end
    #1ps if ($realtime != 2.0) begin errs++; $error("after #1ps: realtime=%f, expected 2", $realtime); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
