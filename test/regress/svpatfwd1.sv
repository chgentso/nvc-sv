module svpatfwd1;
   typedef struct packed {
      logic [3:0] hi;
      logic [3:0] lo;
   } pair_t;

   initial begin
      if (check_pair('{4'hA, 4'h5}, 8'hA5) !== 1'b1)
         $fatal(1, "positional pattern failed");
      if (check_pair('{lo: 4'h3, hi: 4'hC}, 8'hC3) !== 1'b1)
         $fatal(1, "named field pattern failed");
      $display("PASSED");
   end

   function automatic bit check_pair(pair_t p, logic [7:0] exp);
      return p === exp;
   endfunction
endmodule
