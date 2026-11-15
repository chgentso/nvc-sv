// Statement coverage of the procedural blocks of a module: initial, always,
// always_ff, always_comb, always_latch and final, delays and event controls,
// loops, blocking and non-blocking assignments, system task calls and a
// named block.

// Code in a package is not counted and the call which initialises the
// package is not a statement of the module
package vlogcover1_pkg;
   int base = 1;

   function int get_base();
      return base;
   endfunction
endpackage

module vlogcover1;
   import vlogcover1_pkg::*;

   reg clk = 0;
   reg [3:0] cnt = 0;
   reg [3:0] acc, sum, comb, latch;
   integer i;

   always #5 clk = ~clk;

   // Counted once for each clock edge
   always_ff @(posedge clk) begin
      cnt <= cnt + 1;
      acc <= acc + cnt;
   end

   always_comb begin
      comb = cnt + 1;
   end

   always_latch begin
      if (cnt[0])
         latch = cnt;
   end

   initial begin : setup
      sum = get_base() - 1;
      i = 0;
      while (i < 3) begin
         i = i + 1;
      end
      for (i = 0; i < 2; i = i + 1)
         sum = sum;
      repeat (2) sum = sum + 1;
   end

   initial begin
      #32;
      $display("PASSED");
      $finish;
   end

   final begin
      $display("final");
   end
endmodule
