`timescale 1ns/1ps

// A Verilog net connected to an inout port of a VHDL entity must keep its
// value when the VHDL driver of the port is updated
module mixed44;
   reg clk = 0;
   reg [1:0] drv = 2'bzz;
   wire [1:0] net = drv;
   wire [1:0] seen;

   always #5 clk = ~clk;

   mixed44_sub u (.clk(clk), .io(net), .seen(seen));

   initial begin
      #12 drv = 2'b10;
      #40 if (seen !== 2'b10) $display("FAILED %b", seen);
      drv = 2'b01;
      #40 if (seen !== 2'b01) $display("FAILED %b", seen);
      else $display("PASSED");
      $finish;
   end
endmodule
