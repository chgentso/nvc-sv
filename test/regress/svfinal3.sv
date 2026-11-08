// A final procedure runs when the simulation ends; here $finish at 45 ends it.
// Expected values: IEEE 1800-2017 9.2.3 (final), 9.4.1 (always #10 increments at 10,20,30,40 so n=4 at 45),
// 20.2 ($finish). What a tool-specific stop time does to final procedures is not defined by the LRM
// and is not relied on: the end of simulation is made explicit with $finish.
`timescale 1ns/1ns
module svfinal3;
  int n = 0;
  always #10 n++;
  initial $display("start");
  initial #45 $finish;
  final begin
    if (n !== 4) $error("n must be 4, got %0d", n);
    else if ($time != 45) $error("$time must be 45, got %0t", $time);
    else $display("PASSED");
  end
endmodule
