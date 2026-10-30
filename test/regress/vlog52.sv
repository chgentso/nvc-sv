// Implicit enum values following an explicit value
module vlog52;
   typedef enum { A = 64, B, C, D } t_e;
   initial begin
      if (B != 65 || C != 66 || D != 67) $display("FAILED %0d %0d", C, D);
      $display("PASSED");
   end
endmodule
