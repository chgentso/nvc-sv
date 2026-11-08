// IEEE 1800-2017 16.12: the disable condition of disable iff is not
// sampled; when it becomes true, the attempts in progress are aborted.  An
// attempt that starts after the condition has gone false again is not
// affected, so a short pulse between two clock edges aborts only the
// attempts in progress at the time of the pulse, and the assertion goes on
// reporting failures afterwards.
`timescale 1ns/1ns
module svsvadisasync2;
  bit clk, rst, a = 1, b = 0;
  typedef int q_t[$];
  int fails[$], errs;

  always #5 clk = ~clk;                 // Edges at 5, 15, 25, ...

  assert property (@(posedge clk) disable iff (rst) a |=> b)
    else fails.push_back($time);

  initial begin
    #7 rst = 1;                         // Aborts the attempt started at 5
    #1 rst = 0;
    #39;
    // Attempts started at 15, 25 and 35 fail one cycle later
    if (fails != q_t'{25, 35, 45}) begin
      $error("failures at %p", fails);
      errs++;
    end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
