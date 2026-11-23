// VPI cbValueChange calling vpi_control(vpiFinish) mid-batch
`timescale 1ns / 1ns

module vpi29;
   logic sig = 0;

   initial begin
      #5 sig = 1;
      #5 $display("UNEXPECTED at 10");
   end
endmodule
