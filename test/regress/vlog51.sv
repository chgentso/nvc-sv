// Comparing two-state values
module vlog51;
   int i, n;
   initial begin
      i = 0;
      n = 1;
      if (!(i < n)) $display("FAILED lt");
      if (i >= n) $display("FAILED geq");
      if ((i < n) !== 1'b1) $display("FAILED known");
      for (int j = 0; j < n; j++) i++;
      if (i != 1) $display("FAILED loop");
      $display("PASSED");
   end
endmodule
