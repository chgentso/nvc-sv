// The diagnostic callback of the nvc VPI extension sees the messages of
// Verilog severity tasks and failing assertions.  The C side is
// test/vpi/vpi14.c.

`timescale 1ns / 1ns

module vpi14;
   int x = 0;

   initial begin
      #1 $warning("careful");
      #1 $info("fyi");
      #1 assert (x == 1) else $warning("x is %0d", x);
      #1 $display("done");
   end
endmodule
