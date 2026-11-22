// Bits driven by each iteration are selected by local parameters with
// conditional expressions of the genvars: each driver is a single bit
module vlog_genshare12;
   logic [15:0] cov;
   for (genvar g = 0; g < 4; g++) begin : a
      localparam int B = (g == 0) ? 2 : 4;
      for (genvar b = 0; b < 2; b++) begin : c
         localparam int Off = g * 4 + B * b / 2 + (!(g > 1) && b == 1);
         assign cov[Off] = 1'b1;
      end
   end
   initial begin
      #1;
      if (cov !== 16'bx1x1x1x11xx1x1x1) $display("FAILED %b", cov);
      else $display("PASSED");
   end
endmodule
