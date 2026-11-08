// Event control with "negedge or event" list.
// Expected values: IEEE 1800-2017 9.4.2 (event control; "or" wakes on any listed event;
// a variable initialised at time 0 does not produce a negedge), 15.5 (named event),
// 9.4.1 (delays).
`timescale 1ns/1ns
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svevorlist1;
  logic clk = 0, rst_n = 1;
  event e;
  int n;
  int err = 0;
  time tw[4];
  initial begin
    fork
      begin
        repeat (3) begin
          @(negedge rst_n or e);
          n++;
          tw[n] = $time;
        end
      end
      begin
        #5 -> e;
        #5 rst_n = 0;
        #5 -> e;
        #5 rst_n = 1;
        #5;
        `CHK(n == 3, "n at 25")
      end
    join
    `CHK(tw[1] == 5 && tw[2] == 10 && tw[3] == 15, "wake times 5, 10, 15")
    `CHK($time == 25, "join completes at 25")
    if (err == 0) $display("PASSED");
    $finish;
  end
endmodule
