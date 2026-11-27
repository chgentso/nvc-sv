`timescale 1ns/100ps
module tcff (input clk, d);
  reg n_h, n_r;
  specify
    $hold(posedge clk, d, 1, n_h);
    $recovery(negedge d, posedge clk, 2, n_r);
  endspecify
  always @(n_h) $display("%0t hold notifier %b", $realtime, n_h);
  always @(n_r) $display("%0t recovery notifier %b", $realtime, n_r);
endmodule

module vlog_tcheck_init1;
  reg clk, d;
  tcff u (clk, d);
  initial begin
    clk = 0; d = 0;           // No reference event yet: no violation
    #10 clk = 1;
    #0.5 d = 1;               // Hold violation 0.5 after the clock edge
    #5 clk = 0;
    #10 $finish;
  end
endmodule
