module funcnb2;
   reg x;

   function integer f1;
      input a;
      begin : nested
         x <= a;         // Error: non-blocking assignment in Verilog function
         f1 = 0;
      end
   endfunction
endmodule
