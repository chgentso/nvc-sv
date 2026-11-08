// A string literal returned from a function
module svstrret1;
   function string f();
      return "A";
   endfunction

   initial begin
      if (f() != "A")
         $display("FAILED");
      else
         $display("PASSED");
   end
endmodule
