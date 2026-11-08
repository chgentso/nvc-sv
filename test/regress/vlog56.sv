// Break and continue
module vlog56;
   initial begin
      automatic int total = 0, k = 0;
      for (int i = 0; i < 5; i++) begin
         if (i == 1) continue;
         if (i == 3) break;
         total += i;
      end
      if (total != 2) $display("FAILED for %0d", total);
      while (1) begin
         k++;
         if (k < 3) continue;
         break;
      end
      if (k != 3) $display("FAILED while %0d", k);
      forever begin
         k++;
         if (k == 10) break;
      end
      if (k != 10) $display("FAILED forever %0d", k);
      $display("PASSED");
   end
endmodule
