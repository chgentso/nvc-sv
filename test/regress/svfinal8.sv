// A final procedure runs at the end of simulation (no $finish: the event queue empties at time 7).
// Expected values: IEEE 1800-2017 9.2.3 (final), 9.4.1 (n=3 from time 7). Whether the $strobe issued from the
// final procedure prints is implementation-defined (it needs a postponed region after the last time step)
// and is not checked.
`timescale 1ns/1ns
module svfinal8;
  int n = 0;
  initial begin #7 n = 3; end
  final begin
    $strobe("strobe n=%0d", n);
    if (n !== 3) $error("n must be 3, got %0d", n);
    else $display("PASSED");
  end
endmodule
