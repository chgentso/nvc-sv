`timescale 1ns/1ps
module vlog_timelit_div1;
  localparam tCK = 1ns;
  real t0;

  initial begin
    t0 = $realtime;
    #(tCK / 2);
    if ($realtime - t0 < 0.49 || $realtime - t0 > 0.51) begin
      $display("FAILED: dt=%f", $realtime - t0);
      $finish;
    end
    $display("PASSED");
  end
endmodule
