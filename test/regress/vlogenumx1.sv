// Enumeration values which differ only in their unknown bits are distinct
// (IEEE 1800-2017 6.19).

module vlogenumx1;
   enum logic [1:0] { S00 = 2'b00, S0X = 2'b0?, SX0 = 2'b?0, XXX = 2'dx } s;

   initial begin
      s = S0X;
      if (S0X === SX0 || S0X === XXX || SX0 === S00)
         $fatal(1, "values are equal");
      if (S0X !== 2'b0z || SX0 !== 2'bz0) $fatal(1, "values %b %b", S0X, SX0);
      $display("PASSED");
   end
endmodule
