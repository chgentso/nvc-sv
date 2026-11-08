`timescale 1ns/1ns
// A global clocking declaration alongside ordinary clocked logic.
// Expected values: IEEE 1800-2017 14.14 (global clocking declaration; it does not
// change the behaviour of other processes), 4.7 / 9.4.2 (nonblocking assignment
// q <= d at posedge clk), 9.4.1 (#delay).
// clk has posedges at 5, 15, 25.  d is set to 1 at 12, so the posedge at 15 loads
// q = 1; the first check (16 ns) and the second (26 ns) both see q = 1.
module svaglobclk1;
  logic clk = 0, d = 0, q = 0;
  int errors = 0;
  always #5 clk = ~clk;
  global clocking ck1 @(posedge clk); endclocking
  always @(posedge clk) q <= d;
  initial begin
    #12 d = 1;
    @(posedge clk); #1
    if ($time !== 16 || q !== 1'b1) begin $error("at %0t: q = %b, expected 1 at 16", $time, q); errors++; end
    @(posedge clk); #1
    if ($time !== 26 || q !== 1'b1) begin $error("at %0t: q = %b, expected 1 at 26", $time, q); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
