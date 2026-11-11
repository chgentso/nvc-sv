// A function whose arguments all have default values can be called without
// parentheses (13.5.3)
module svfuncdefarg1;
   class c;
      int v = 3;

      virtual function int unsigned getv(int x = 0, int y = 1);
         return v + x + y;
      endfunction

      function int test(int unsigned a);
         if (a > getv) return 1;
         else if (a == getv) return 2;
         return 0;
      endfunction

      function int unsigned plus();
         return getv + 1;
      endfunction
   endclass

   function automatic int g(int k = 5);
      return k;
   endfunction

   initial begin
      automatic c o = new;
      int y = g + 1;
      if (o.test(5) !== 1) $fatal(1, "test 5");
      if (o.test(4) !== 2) $fatal(1, "test 4");
      if (o.plus() !== 5) $fatal(1, "plus %0d", o.plus());
      if (y !== 6) $fatal(1, "y %0d", y);
      $display("PASSED");
   end
endmodule
