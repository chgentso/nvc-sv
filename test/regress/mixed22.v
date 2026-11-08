`timescale 1ns/1ns
// The test is expected to fail so that run_regr elaborates and runs
// separately, which recreates the placeholder units for the shims
module mixed22;
   e22 u();
   e22 v();

   initial begin
      #2;
      $fatal(1, "end of test");
   end
endmodule
