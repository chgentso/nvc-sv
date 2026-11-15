   // Included in the body of a module
   for (genvar i = 0; i < 2; i++) begin : blk
      initial begin
         x = x + i;
         x = x + 1;
      end
   end
