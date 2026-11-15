module mixedcover1_sub (input a, output reg y);
   always @(a) begin
      if (a)
         y = 1;
      else
         y = 0;
   end
endmodule
