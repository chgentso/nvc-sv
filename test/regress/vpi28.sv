// VPI cbNextSimTime / cbValueChange / cbAfterDelay + vpi_control(vpiFinish)
`timescale 1ns / 1ns

module vpi28;
   logic sig = 0;

   initial begin
      $display("at 0");
      #5 sig = 1;
      $display("at 5");
      #5 $display("UNEXPECTED at 10");
   end
endmodule
