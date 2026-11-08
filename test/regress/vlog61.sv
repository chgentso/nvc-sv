// Casts to simple types
module vlog61;
   initial begin
      real r;
      automatic int  i = 7;
      r = real'(i) / 2.0;
      if (r != 3.5) $display("FAILED real");
      if (int'(r) != 4) $display("FAILED int %0d", int'(r));
      $display("PASSED");
   end
endmodule
