module funcnb1;
   reg x;

   function int f1(input logic a);
      begin : nested
         x <= a;         // Error: non-blocking assignment in function
         return 0;
      end
   endfunction

   function void f2(input logic a);
      fork
         x <= a;         // OK: inside fork...join_none
      join_none
   endfunction

   final begin
      x <= 1'b0;         // Error: non-blocking assignment in final
   end
endmodule
