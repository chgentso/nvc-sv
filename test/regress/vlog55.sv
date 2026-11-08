// Parts of two-state variables, and delay from a variable
module vlog55;
   typedef int unsigned u32_t;
   task automatic check();
      u32_t x;
      int y;
      time d;
      x = 0;
      x[3] = 1'b1;
      if (x != 8) $display("FAILED write %0d", x);
      if (x[3] !== 1'b1 || x[2] !== 1'b0) $display("FAILED read");
      y = 'ha5;
      if (y[7:4] !== 4'ha) $display("FAILED part");
      d = 5;
      #(d);
      if ($time == 0) $display("FAILED delay");
   endtask

   initial begin
      check();
      $display("PASSED");
   end
endmodule
