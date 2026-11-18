// A generate block may appear directly as a module item without the
// generate keyword.  IEEE 1800-2017 27.3, 27.6
module vlogbareblk1;
   wire [3:0] w;

   begin : g1
      assign w = 4'd5;
      logic x = 1;
   end

   begin                                // genblk2
      wire y = 1'b1;
      begin : inner
         wire z = 1'b0;
      end
   end

   initial begin
      #1;
      if (w !== 5) $fatal(1, "w = %b", w);
      if (g1.x !== 1) $fatal(1, "g1.x");
      if (genblk2.y !== 1) $fatal(1, "genblk2.y");
      if (genblk2.inner.z !== 0) $fatal(1, "genblk2.inner.z");
      $display("PASSED");
   end
endmodule
