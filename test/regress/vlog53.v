// Instance directly in an if generate with no begin
module vlog53;
   parameter WITH = 1;
   wire y;

   generate
      if (WITH)
         sub1 #(.W(8)) u1 (.x(1'b1), .y(y));
      else
         assign y = 0;
   endgenerate

   initial begin
      #1;
      if (y !== 1'b1) $display("FAILED");
      $display("PASSED");
   end
endmodule

module sub1 #(parameter W = 1) (input x, output y);
   assign y = x;
endmodule
