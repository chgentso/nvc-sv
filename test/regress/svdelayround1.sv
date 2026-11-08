// Delays are rounded to the time precision of the module, with a decimal
// value half way between two steps rounded away from zero
// IEEE 1800-2017 5.8 (a time literal is scaled to the time unit and rounded
// to the time precision), 3.14.2.1 / 22.7 (delays are rounded to the
// precision of the module), 14.4 (1step is the global precision)
`timescale 1ns/10ps
module svdelayround1_fine;
  timeunit 1ns;
  timeprecision 1ps;
  initial begin
    #3;
    #1step;
    svdelayround1.fine_done = $realtime;
  end
endmodule

`timescale 1ns/10ps
module svdelayround1;
  real d = 2.675;
  realtime fine_done;
  int errs;
  realtime t0;
  svdelayround1_fine fine();
  task automatic ck(string tag, realtime got, realtime exp);
    if (got < exp - 1e-9 || got > exp + 1e-9) begin
      $display("FAILED %s: got %f expected %f", tag, got, exp); errs++;
    end
  endtask
  initial begin
    t0 = $realtime; #2.675;   ck("real literal tie", $realtime - t0, 2.68);
    t0 = $realtime; #d;       ck("real variable tie", $realtime - t0, 2.68);
    t0 = $realtime; #0.125;   ck("exact tie", $realtime - t0, 0.13);
    t0 = $realtime; #1.005;   ck("binary below tie", $realtime - t0, 1.01);
    t0 = $realtime; #0.004ns; ck("time literal down", $realtime - t0, 0.0);
    t0 = $realtime; #0.005ns; ck("time literal tie", $realtime - t0, 0.01);
    t0 = $realtime; #1.5ps;   ck("time literal ps", $realtime - t0, 0.0);
    t0 = $realtime; #2.675ns; ck("time literal ns tie", $realtime - t0, 2.68);
    ck("1step is the global precision", fine_done, 3.001);
    if (errs == 0) $display("PASSED");
  end
endmodule
