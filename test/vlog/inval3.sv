module inval3;
   real r = 0.5;
   logic [3:0] b;
   logic c;

   function void f(int a);
      return a;
   endfunction

   initial c = b[r];
endmodule
