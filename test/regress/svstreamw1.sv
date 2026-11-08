module svstreamw1;
   int a = 1, b = 2;
   initial begin
      automatic int d = {<<{a, b}};
   end
endmodule
